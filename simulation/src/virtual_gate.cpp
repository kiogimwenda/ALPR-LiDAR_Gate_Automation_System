// virtual_gate.cpp — physics + the GateController wiring, host edition.

#include "gate_sim/virtual_gate.hpp"

#include <cstdio>

namespace gate::sim {

namespace sm = gate::state_machine;

VirtualGate::VirtualGate(const Config& cfg)
    : cfg_(cfg),
      machine_(sm::Config{
          .gate_type = sm::GateType::Sliding,
          .motor_timeout_ms = cfg.motor_timeout_ms,
          .auto_close_ms = cfg.auto_close_ms,
      }) {}

void VirtualGate::boot_closed() {
    const std::lock_guard<std::mutex> lock(mutex_);
    position_ms_ = 0.0;
    apply_locked(machine_.init_closed());
}

void VirtualGate::issue(const std::string& command_id, Command c) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto terminal = (c == Command::Open) ? sm::State::Open : sm::State::Closed;
    record(tracker_.arm(command_id.c_str(), terminal,
                        static_cast<std::uint32_t>(now_ms_) + cfg_.command_deadline_ms));
    step_locked(c == Command::Open ? sm::Event::CommandOpen : sm::Event::CommandClose);
}

void VirtualGate::local_open() {
    const std::lock_guard<std::mutex> lock(mutex_);
    step_locked(sm::Event::CommandOpen);
}

void VirtualGate::local_close() {
    const std::lock_guard<std::mutex> lock(mutex_);
    step_locked(sm::Event::CommandClose);
}

void VirtualGate::operator_stop() {
    const std::lock_guard<std::mutex> lock(mutex_);
    step_locked(sm::Event::CommandStop);
}

void VirtualGate::clear_fault() {
    const std::lock_guard<std::mutex> lock(mutex_);
    step_locked(sm::Event::FaultCleared);
    if (machine_.state() == sm::State::Initializing) {
        resolve_position_locked();
    }
}

void VirtualGate::reboot() {
    const std::lock_guard<std::mutex> lock(mutex_);
    // A reboot loses volatile state but not physical position; the
    // driver layer re-reads the limit switches on boot (4.5.2).
    motor_open_ = false;
    motor_close_ = false;
    watchdog_deadline_ = 0;
    autoclose_deadline_ = 0;
    machine_ = sm::StateMachine(sm::Config{
        .gate_type = sm::GateType::Sliding,
        .motor_timeout_ms = cfg_.motor_timeout_ms,
        .auto_close_ms = cfg_.auto_close_ms,
    });
    if (beam_blocked_) {
        (void)machine_.handle(sm::Event::SafetyBeamTripped);  // re-latch
    }
    resolve_position_locked();
}

void VirtualGate::trip_beam() {
    const std::lock_guard<std::mutex> lock(mutex_);
    beam_blocked_ = true;
    step_locked(sm::Event::SafetyBeamTripped);
}

void VirtualGate::clear_beam() {
    const std::lock_guard<std::mutex> lock(mutex_);
    beam_blocked_ = false;
    step_locked(sm::Event::SafetyBeamCleared);
}

void VirtualGate::advance(std::uint32_t dt_ms) {
    const std::lock_guard<std::mutex> lock(mutex_);
    now_ms_ += dt_ms;
    record(tracker_.on_tick(static_cast<std::uint32_t>(now_ms_)));

    if (motor_open_) {
        position_ms_ += dt_ms;
        if (position_ms_ >= cfg_.travel_ms) {
            position_ms_ = cfg_.travel_ms;
            step_locked(sm::Event::LimitOpenHit);
        }
    } else if (motor_close_) {
        position_ms_ -= dt_ms;
        if (position_ms_ <= 0.0) {
            position_ms_ = 0.0;
            step_locked(sm::Event::LimitClosedHit);
        }
    }

    if (watchdog_deadline_ != 0 && now_ms_ >= watchdog_deadline_) {
        watchdog_deadline_ = 0;
        step_locked(sm::Event::MotorTimeout);
    }
    if (autoclose_deadline_ != 0 && now_ms_ >= autoclose_deadline_) {
        autoclose_deadline_ = 0;
        step_locked(sm::Event::CommandClose);
    }
}

sm::State VirtualGate::state() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return machine_.state();
}

bool VirtualGate::limit_open() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return position_ms_ >= cfg_.travel_ms;
}

bool VirtualGate::limit_closed() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return position_ms_ <= 0.0;
}

bool VirtualGate::beam_clear() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return !beam_blocked_;
}

std::vector<VirtualGate::Ack> VirtualGate::drain_acks() {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Ack> out;
    out.swap(acks_);
    return out;
}

std::string VirtualGate::status_line() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "state=%s position=%.0f%% beam=%s motor=%s tracker=%s",
                  sm::to_string(machine_.state()).data(), (position_ms_ / cfg_.travel_ms) * 100.0,
                  beam_blocked_ ? "BLOCKED" : "clear",
                  motor_open_ ? "open" : (motor_close_ ? "close" : "off"),
                  tracker_.armed() ? "pending" : "idle");
    return buf;
}

// ---------- internals ---------------------------------------------------------

void VirtualGate::step_locked(sm::Event ev) {
    apply_locked(machine_.handle(ev));
}

// Mirrors GateController: execute → manage_timers → listener → policies.
void VirtualGate::apply_locked(const sm::Outputs& out) {
    for (std::uint8_t i = 0; i < out.count; ++i) {
        switch (out.actions[i]) {
            case sm::Action::DriveMotorOpen:
                motor_close_ = false;
                motor_open_ = true;
                break;
            case sm::Action::DriveMotorClose:
                motor_open_ = false;
                motor_close_ = true;
                break;
            case sm::Action::StopMotor:
                motor_open_ = false;
                motor_close_ = false;
                break;
            default:
                break;  // LEDs / telemetry have no physics
        }
    }

    if (out.transitioned) {
        const bool moving =
            out.new_state == sm::State::Opening || out.new_state == sm::State::Closing;
        watchdog_deadline_ = moving ? now_ms_ + cfg_.motor_timeout_ms : 0;
        autoclose_deadline_ = (out.new_state == sm::State::Open && cfg_.auto_close_ms > 0)
                                  ? now_ms_ + cfg_.auto_close_ms
                                  : 0;
    }

    if (out.transitioned || out.reason != sm::Reason::None) {
        record(tracker_.on_gate_event(out.new_state, out.reason, out.transitioned));
    }

    if (cfg_.reverse_on_beam && out.transitioned && out.new_state == sm::State::StoppedClose &&
        out.reason == sm::Reason::SafetyBeamObstacle) {
        step_locked(sm::Event::CommandOpen);
        return;
    }
    if (!out.transitioned && out.new_state == sm::State::Open &&
        out.reason == sm::Reason::SafetyBeamObstacle && cfg_.auto_close_ms > 0) {
        autoclose_deadline_ = now_ms_ + cfg_.auto_close_ms;
    }
}

void VirtualGate::resolve_position_locked() {
    if (position_ms_ <= 0.0) {
        apply_locked(machine_.init_closed());
    } else if (position_ms_ >= cfg_.travel_ms) {
        apply_locked(machine_.init_open());
    } else {
        apply_locked(machine_.init_unknown());  // mid-travel: operator must reset
    }
}

void VirtualGate::record(const gate::rpc::CommandTracker::Verdict& v) {
    if (v.fire) {
        acks_.push_back({v.command_id, v.success, v.error});
    }
}

}  // namespace gate::sim
