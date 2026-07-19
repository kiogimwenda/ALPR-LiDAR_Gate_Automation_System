// gate_controller.cpp — event pump, action dispatch, and timer ownership.

#include "gate_control/gate_controller.hpp"

#include <esp_log.h>

namespace gate::control {

namespace sm = gate::state_machine;

namespace {

constexpr const char* kTag = "gate-ctrl";

using Action = sm::Action;

// One slot per producer would be 5; 16 gives headroom for a burst
// (e.g. beam chatter right as a limit lands) without meaningful RAM
// cost. Events are edge-triggered and the pump drains fast, so a full
// queue means something is deeply wrong — we log the drop and move on.
constexpr UBaseType_t kQueueDepth = 16;

// ESP-IDF's xTaskCreate takes the stack depth in bytes (a deliberate
// deviation from vanilla FreeRTOS's words).
constexpr std::uint32_t kPumpStackBytes = 4096;
constexpr UBaseType_t kPumpPriority = 5;

// Boot settle time before the first limit-switch read: covers the
// slowest debounce commit among the inputs (limit switches: 20 ms
// debounce at a 5 ms poll; beam: 5 ms at 1 ms) with margin.
constexpr std::uint32_t kBootSettleMs = 50;

}  // namespace

GateController::GateController(const Config& cfg)
    : cfg_(cfg),
      relay_open_({.gpio = cfg.pins.relay_open}),
      relay_close_({.gpio = cfg.pins.relay_close}),
      limit_open_({.gpio = cfg.pins.limit_open}),
      limit_closed_({.gpio = cfg.pins.limit_closed}),
      beam_({.gpio = cfg.pins.safety_beam}),
      led_({.red_gpio = cfg.pins.led_red,
            .green_gpio = cfg.pins.led_green,
            .yellow_gpio = cfg.pins.led_yellow}),
      sm_(cfg.sm) {
    queue_ = xQueueCreate(kQueueDepth, sizeof(Event));
    configASSERT(queue_ != nullptr);

    esp_timer_create_args_t targs = {};
    targs.dispatch_method = ESP_TIMER_TASK;
    targs.arg = this;

    targs.name = "gate_watchdog";
    targs.callback = [](void* arg) {
        static_cast<GateController*>(arg)->post(Event::MotorTimeout);
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &watchdog_timer_));

    targs.name = "gate_autoclose";
    targs.callback = [](void* arg) {
        static_cast<GateController*>(arg)->post(Event::CommandClose);
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &autoclose_timer_));
}

GateController::~GateController() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
    if (watchdog_timer_ != nullptr) {
        esp_timer_stop(watchdog_timer_);
        esp_timer_delete(watchdog_timer_);
        watchdog_timer_ = nullptr;
    }
    if (autoclose_timer_ != nullptr) {
        esp_timer_stop(autoclose_timer_);
        esp_timer_delete(autoclose_timer_);
        autoclose_timer_ = nullptr;
    }
    if (queue_ != nullptr) {
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
}

esp_err_t GateController::start() {
    if (started_) {
        return ESP_ERR_INVALID_STATE;
    }
    started_ = true;

    // Let every input debouncer commit its first real sample before we
    // trust is_active()/is_blocked() — they all start pessimistically
    // at "inactive" and need debounce_ms of stable polls to flip.
    vTaskDelay(pdMS_TO_TICKS(kBootSettleMs));

    // Pre-latch the beam state. In Initializing the state machine drops
    // the transition but still records the latch, so a CommandClose
    // issued right after boot with something in the gate path is
    // rejected exactly like one issued at runtime.
    if (beam_.is_blocked()) {
        ESP_LOGW(kTag, "safety beam blocked at boot — close commands will be rejected");
        (void)sm_.handle(Event::SafetyBeamTripped);
    }

    resolve_boot_position();

    // Register edge callbacks only after the position is resolved so
    // the pump queue starts from a clean slate. All three fire from the
    // esp_timer task (never an ISR) — plain xQueueSend is safe.
    limit_open_.set_callback(
        [this](bool active) { post(active ? Event::LimitOpenHit : Event::LimitOpenReleased); });
    limit_closed_.set_callback(
        [this](bool active) { post(active ? Event::LimitClosedHit : Event::LimitClosedReleased); });
    beam_.set_callback([this](bool blocked) {
        post(blocked ? Event::SafetyBeamTripped : Event::SafetyBeamCleared);
    });

    if (xTaskCreate(&GateController::pump_task_entry, "gate_ctrl", kPumpStackBytes, this,
                    kPumpPriority, &task_) != pdPASS) {
        ESP_LOGE(kTag, "failed to create gate_ctrl task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(kTag, "started (state=%.*s, reverse_on_beam=%d, auto_close=%ums)",
             static_cast<int>(sm::to_string(state()).size()), sm::to_string(state()).data(),
             cfg_.reverse_on_beam ? 1 : 0, static_cast<unsigned>(cfg_.sm.auto_close_ms));
    return ESP_OK;
}

void GateController::command_open() {
    post(Event::CommandOpen);
}
void GateController::command_close() {
    post(Event::CommandClose);
}
void GateController::command_stop() {
    post(Event::CommandStop);
}
void GateController::clear_fault() {
    post(Event::FaultCleared);
}

sm::State GateController::state() const {
    return state_snapshot_.load(std::memory_order_relaxed);
}

sm::Reason GateController::reason() const {
    return reason_snapshot_.load(std::memory_order_relaxed);
}

bool GateController::limit_open_active() const {
    return limit_open_.is_active();
}

bool GateController::limit_closed_active() const {
    return limit_closed_.is_active();
}

bool GateController::beam_blocked() const {
    return beam_.is_blocked();
}

void GateController::set_transition_listener(TransitionListener listener) {
    configASSERT(!started_);  // pump reads it unlocked; set-before-start only
    listener_ = std::move(listener);
}

void GateController::show_led_pattern(drivers::StatusLed::Pattern p) {
    led_.render(p);
}

// ---------- pump task ---------------------------------------------------------

void GateController::pump_task_entry(void* arg) {
    static_cast<GateController*>(arg)->pump_task();
}

void GateController::pump_task() {
    for (;;) {
        Event ev;
        if (xQueueReceive(queue_, &ev, pdMS_TO_TICKS(cfg_.heartbeat_period_ms)) == pdTRUE) {
            const Outputs out = sm_.handle(ev);
            ESP_LOGD(kTag, "event %.*s -> state %.*s (transitioned=%d)",
                     static_cast<int>(sm::to_string(ev).size()), sm::to_string(ev).data(),
                     static_cast<int>(sm::to_string(out.new_state).size()),
                     sm::to_string(out.new_state).data(), out.transitioned ? 1 : 0);
            execute(out);
            manage_timers(out);
            apply_policies(out);

            // Surface both transitions and reasoned rejections to the
            // RPC layer — a close refused by the beam latch does not
            // change state, but a pending CommandAck must hear it.
            if (listener_ && (out.transitioned || out.reason != sm::Reason::None)) {
                listener_(out.new_state, out.reason, out.transitioned);
            }

            // FaultCleared lands the machine back in Initializing; the
            // header contract says the driver layer must re-verify the
            // physical position before any motion is possible.
            if (ev == Event::FaultCleared && out.transitioned &&
                out.new_state == State::Initializing) {
                ESP_LOGI(kTag, "fault cleared — re-resolving gate position");
                resolve_boot_position();
            }
        } else {
            // Queue timeout — heartbeat. Confirms the pump is alive and
            // gives the serial console a periodic state snapshot until
            // real telemetry lands in Phase 4.5.4.
            ESP_LOGI(kTag, "heartbeat: state=%.*s reason=%.*s beam=%s limits[open=%d closed=%d]",
                     static_cast<int>(sm::to_string(state()).size()), sm::to_string(state()).data(),
                     static_cast<int>(sm::to_string(reason()).size()),
                     sm::to_string(reason()).data(), beam_.is_blocked() ? "blocked" : "clear",
                     limit_open_.is_active() ? 1 : 0, limit_closed_.is_active() ? 1 : 0);
        }
    }
}

// ---------- helpers -----------------------------------------------------------

void GateController::post(Event ev) {
    if (xQueueSend(queue_, &ev, 0) != pdTRUE) {
        ESP_LOGW(kTag, "event queue full — dropped %.*s",
                 static_cast<int>(sm::to_string(ev).size()), sm::to_string(ev).data());
    }
}

void GateController::resolve_boot_position() {
    const bool open_active = limit_open_.is_active();
    const bool closed_active = limit_closed_.is_active();

    Outputs out;
    if (closed_active && !open_active) {
        out = sm_.init_closed();
    } else if (open_active && !closed_active) {
        out = sm_.init_open();
    } else {
        ESP_LOGE(kTag, "cannot resolve position (open=%d closed=%d) — %s", open_active ? 1 : 0,
                 closed_active ? 1 : 0,
                 (open_active && closed_active) ? "both limits asserted: wiring fault"
                                                : "mid-travel: operator must reset");
        out = sm_.init_unknown();
    }
    execute(out);
    manage_timers(out);
}

void GateController::execute(const Outputs& out) {
    for (std::uint8_t i = 0; i < out.count; ++i) {
        switch (out.actions[i]) {
            case Action::DriveMotorOpen:
                // Break-before-make: the opposing contactor is dropped
                // before the driving one is picked so both coils are
                // never energised together, whatever order the state
                // machine emitted.
                relay_close_.set(false);
                relay_open_.set(true);
                break;
            case Action::DriveMotorClose:
                relay_open_.set(false);
                relay_close_.set(true);
                break;
            case Action::StopMotor:
                relay_open_.set(false);
                relay_close_.set(false);
                break;
            case Action::SetLedClosed:
                led_.render(drivers::StatusLed::Pattern::kOff);
                break;
            case Action::SetLedOpening:
            case Action::SetLedClosing:
                led_.render(drivers::StatusLed::Pattern::kGateMoving);
                break;
            case Action::SetLedOpen:
                led_.render(drivers::StatusLed::Pattern::kGateOpen);
                break;
            case Action::SetLedStopped:
                led_.render(drivers::StatusLed::Pattern::kGateStopped);
                break;
            case Action::SetLedFault:
                led_.render(drivers::StatusLed::Pattern::kFaultSlow);
                break;
            case Action::EmitTelemetryStateChanged:
                // Placeholder until the gRPC Control stream lands in
                // Phase 4.5.4 — the log line is the "telemetry" for now.
                ESP_LOGI(kTag, "state -> %.*s (reason=%.*s)",
                         static_cast<int>(sm::to_string(out.new_state).size()),
                         sm::to_string(out.new_state).data(),
                         static_cast<int>(sm::to_string(out.reason).size()),
                         sm::to_string(out.reason).data());
                break;
            case Action::None:
                break;
        }
    }
    state_snapshot_.store(sm_.state(), std::memory_order_relaxed);
    reason_snapshot_.store(sm_.reason(), std::memory_order_relaxed);
}

void GateController::manage_timers(const Outputs& out) {
    if (!out.transitioned) {
        return;
    }

    // Motor watchdog: armed with the full budget on every entry into a
    // moving state (a resume from Stopped gets the full budget again —
    // travel time from mid-position is strictly shorter, so the bound
    // still holds); cancelled the moment motion ends for any reason.
    const bool moving = out.new_state == State::Opening || out.new_state == State::Closing;
    if (esp_timer_is_active(watchdog_timer_)) {
        esp_timer_stop(watchdog_timer_);
    }
    if (moving && cfg_.sm.motor_timeout_ms > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(watchdog_timer_,
                                             std::uint64_t{cfg_.sm.motor_timeout_ms} * 1000ULL));
    }

    // Auto-close countdown: armed on entering Open (when enabled),
    // cancelled on leaving it. Fires a plain CommandClose, which goes
    // through the same queue — and the same beam-latch rejection — as
    // an operator command.
    if (esp_timer_is_active(autoclose_timer_)) {
        esp_timer_stop(autoclose_timer_);
    }
    if (out.new_state == State::Open && cfg_.sm.auto_close_ms > 0) {
        ESP_ERROR_CHECK(
            esp_timer_start_once(autoclose_timer_, std::uint64_t{cfg_.sm.auto_close_ms} * 1000ULL));
    }
}

void GateController::apply_policies(const Outputs& out) {
    // Reverse-on-beam: the state machine stopped the motor on a beam
    // break during Closing; policy says drive back open so the gate
    // moves away from whatever tripped the beam. Opening with the beam
    // still blocked is legal by design.
    if (cfg_.reverse_on_beam && out.transitioned && out.new_state == State::StoppedClose &&
        out.reason == sm::Reason::SafetyBeamObstacle) {
        ESP_LOGW(kTag, "beam break during close — reversing to open (reverse_on_beam policy)");
        post(Event::CommandOpen);
        return;
    }

    // Auto-close retry: an auto-close CommandClose that arrives while
    // the beam is blocked is rejected (gate stays Open, no transition).
    // The one-shot has already fired, so without a re-arm the gate
    // would stay open forever. Re-arm for another full period; the
    // retry loop ends when a close finally goes through or something
    // else moves the gate.
    if (!out.transitioned && out.new_state == State::Open &&
        out.reason == sm::Reason::SafetyBeamObstacle && cfg_.sm.auto_close_ms > 0) {
        ESP_LOGW(kTag, "auto-close blocked by safety beam — retrying in %ums",
                 static_cast<unsigned>(cfg_.sm.auto_close_ms));
        if (esp_timer_is_active(autoclose_timer_)) {
            esp_timer_stop(autoclose_timer_);
        }
        ESP_ERROR_CHECK(
            esp_timer_start_once(autoclose_timer_, std::uint64_t{cfg_.sm.auto_close_ms} * 1000ULL));
    }
}

}  // namespace gate::control
