// gate_controller.hpp — the driver layer that animates the gate state machine.
//
// Phase 4.5.1 delivered a pure state machine: Events in, Actions out,
// no hardware, no clock. This class is the other half of that contract.
// It owns:
//
//   - The hardware drivers (two motor relays, two limit switches, the
//     safety beam, the status LED).
//   - A FreeRTOS task ("gate_ctrl") that pumps Events off a queue,
//     feeds them to the StateMachine, and executes the returned Action
//     list against the drivers.
//   - The two timers the state machine deliberately does not know
//     about: the motor-runtime watchdog (armed while Opening/Closing,
//     fires Event::MotorTimeout) and the auto-close countdown (armed
//     while Open when Config::sm.auto_close_ms > 0, fires
//     Event::CommandClose).
//
// Event sources and threading
// ---------------------------
// Every input driver (LimitSwitch, SafetyBeam) fires its edge callback
// from the esp_timer task — not an ISR — so callbacks post to the event
// queue with plain xQueueSend. The watchdog/auto-close one-shots are
// esp_timer callbacks too. The command_*() entry points may be called
// from any task (today: app_main; Phase 4.5.4: the gRPC Control-stream
// task). All producers converge on one queue; only the gate_ctrl task
// touches the StateMachine, so the state machine itself needs no locks.
//
// Boot-time position resolution
// -----------------------------
// start() reads both limit switches once (after their debounce settles)
// and calls exactly one of init_closed()/init_open()/init_unknown().
// Both-limits-asserted is a wiring fault and neither-asserted is an
// unknown mid-travel position — both resolve to init_unknown(), which
// faults the machine until an operator clears it. clear_fault() returns
// the machine to Initializing, and the pump task re-runs the same
// resolution so the gate can never move from an unverified position.
//
// Reverse-on-beam policy
// ----------------------
// When the safety beam trips during Closing, the state machine stops
// the motor (StoppedClose / SafetyBeamObstacle). Whether the gate then
// reverses to fully open is an operator-policy decision, so it lives
// here, not in the state machine: with Config::reverse_on_beam (default
// true, matching UL 325 entrapment-protection expectations for
// residential operators) the controller posts CommandOpen right after
// the stop, and the normal StoppedClose → Opening transition drives the
// gate away from the obstruction.

#pragma once

#include <atomic>
#include <cstdint>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <functional>

#include "gate_drivers/limit_switch.hpp"
#include "gate_drivers/relay.hpp"
#include "gate_drivers/safety_beam.hpp"
#include "gate_drivers/status_led.hpp"
#include "gate_state_machine/state_machine.hpp"

namespace gate::control {

class GateController {
public:
    // Default pin map for the ESP32-S3 DevKitC-1 reference wiring.
    // GPIO 8–13 are taken by the W5500 (see sdkconfig.defaults); the
    // picks below avoid strapping pins (0, 3, 45, 46), USB D+/D-
    // (19, 20), and the flash/PSRAM range (26–37).
    struct Pins {
        gpio_num_t relay_open = GPIO_NUM_4;    // "drive open" contactor
        gpio_num_t relay_close = GPIO_NUM_5;   // "drive close" contactor
        gpio_num_t limit_open = GPIO_NUM_6;    // open-position switch
        gpio_num_t limit_closed = GPIO_NUM_7;  // closed-position switch
        gpio_num_t safety_beam = GPIO_NUM_15;  // IR beam receiver
        gpio_num_t led_red = GPIO_NUM_16;
        gpio_num_t led_green = GPIO_NUM_17;
        gpio_num_t led_yellow = GPIO_NUM_18;
    };

    struct Config {
        state_machine::Config sm{};
        Pins pins{};
        bool reverse_on_beam = true;
        std::uint32_t heartbeat_period_ms = 5'000;
    };

    // Constructing configures every GPIO and creates (but does not
    // start) the timers and the queue; start() spawns the pump task
    // and resolves the boot position. Split so app_main can construct
    // early (cheap, deterministic) and start once networking is up.
    explicit GateController(const Config& cfg);
    ~GateController();
    GateController(const GateController&) = delete;
    GateController& operator=(const GateController&) = delete;
    GateController(GateController&&) = delete;
    GateController& operator=(GateController&&) = delete;

    // Resolve the boot position from the limit switches, register the
    // driver callbacks, and spawn the pump task. Returns ESP_OK once
    // the task is running; ESP_ERR_INVALID_STATE on a second call.
    esp_err_t start();

    // Command entry points — thread-safe, non-blocking; each posts one
    // Event to the pump queue and returns immediately.
    void command_open();
    void command_close();
    void command_stop();
    void clear_fault();

    // Snapshot of the last committed state/reason, updated by the pump
    // task after every transition. Safe from any task; feeds the
    // heartbeat log and the Telemetry stream.
    [[nodiscard]] state_machine::State state() const;
    [[nodiscard]] state_machine::Reason reason() const;

    // Raw input snapshots for telemetry — lock-free reads of the
    // drivers' committed (debounced) values.
    [[nodiscard]] bool limit_open_active() const;
    [[nodiscard]] bool limit_closed_active() const;
    [[nodiscard]] bool beam_blocked() const;

    // Observer for the RPC layer (Phase 4.5.4): invoked from the pump
    // task after a step that either transitioned or was rejected with
    // a Reason (e.g. a close refused by the beam latch — no state
    // change, but exactly what a pending CommandAck needs to hear).
    // Must be set before start(); the pump reads it without locking.
    using TransitionListener =
        std::function<void(state_machine::State, state_machine::Reason, bool transitioned)>;
    void set_transition_listener(TransitionListener listener);

    // Render a server-requested LED pattern (COMMAND_KIND_LED_PATTERN).
    // Transient overlays (auth/deny flash) simply play out; the next
    // gate transition re-renders the state pattern. Thread-safe.
    void show_led_pattern(drivers::StatusLed::Pattern p);

private:
    using Event = state_machine::Event;
    using Outputs = state_machine::Outputs;
    using State = state_machine::State;

    static void pump_task_entry(void* arg);
    void pump_task();

    void post(Event ev);
    void resolve_boot_position();
    void execute(const Outputs& out);
    void manage_timers(const Outputs& out);
    void apply_policies(const Outputs& out);

    Config cfg_;

    drivers::Relay relay_open_;
    drivers::Relay relay_close_;
    drivers::LimitSwitch limit_open_;
    drivers::LimitSwitch limit_closed_;
    drivers::SafetyBeam beam_;
    drivers::StatusLed led_;

    state_machine::StateMachine sm_;
    std::atomic<State> state_snapshot_{State::Initializing};
    std::atomic<state_machine::Reason> reason_snapshot_{state_machine::Reason::None};
    TransitionListener listener_;

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    esp_timer_handle_t watchdog_timer_ = nullptr;
    esp_timer_handle_t autoclose_timer_ = nullptr;
    bool started_ = false;
};

}  // namespace gate::control
