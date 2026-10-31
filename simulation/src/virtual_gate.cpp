// virtual_gate.cpp — physics + the GateController wiring, host edition.

#include "gate_sim/virtual_gate.hpp"

#include <cstdio>
#include <string_view>

namespace gate::sim {

namespace sm = gate::state_machine;

VirtualGate::VirtualGate(const Config& cfg)
    : cfg_(cfg),
      machine_(sm::Config{
          .gate_type = cfg.gate_type,
          .motor_timeout_ms = cfg.motor_timeout_ms,
          .auto_close_ms = cfg.auto_close_ms,
          .lock_layout = cfg.lock_layout,
          .profile = cfg.profile,
      }),
      sequencer_(cfg.lock_layout, cfg.lock_timing),
      actual_type_(cfg.gate_type) {}

sm::Config VirtualGate::machine_config() const {
    return machine_.config();
}

bool VirtualGate::locked_gate() const {
    return cfg_.lock_layout != sm::LockLayout::None;
}

bool VirtualGate::dual() const {
    return cfg_.gate_type == sm::GateType::DualLeaf;
}

bool VirtualGate::at_open() const {
    return position_ms_ >= cfg_.travel_ms && (!dual() || position_b_ms_ >= cfg_.travel_ms);
}

bool VirtualGate::at_closed() const {
    return position_ms_ <= 0.0 && (!dual() || position_b_ms_ <= 0.0);
}

void VirtualGate::boot_closed() {
    const std::lock_guard<std::mutex> lock(mutex_);
    position_ms_ = 0.0;
    position_b_ms_ = 0.0;
    if (locked_gate()) {
        sequencer_.assume_engaged();
        bolt_ms_ = {};
        mag_bonded_ = {true, true, cfg_.lock_layout == sm::LockLayout::Five};
    }
    apply_locked(machine_.init_closed());
}

void VirtualGate::issue(const std::string& command_id, Command c) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto terminal = (c == Command::kOpen) ? sm::State::Open : sm::State::Closed;
    record(tracker_.arm(command_id.c_str(), terminal,
                        static_cast<std::uint32_t>(now_ms_) + cfg_.command_deadline_ms));
    step_locked(c == Command::kOpen ? sm::Event::CommandOpen : sm::Event::CommandClose);
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
    verify_deadline_ = 0;
    calibrate_deadline_ = 0;
    bolt_rest_deadline_ = 0;
    permit_ = false;
    const bool latched = machine_.lockdown_latched();  // NVS survives the reset
    machine_ = sm::StateMachine(machine_config());
    machine_.restore_lockdown(latched);
    // Reset state: bolts drop on their springs, maglocks lose power.
    sequencer_ = sm::LockSequencer(cfg_.lock_layout, cfg_.lock_timing);
    bolt_ms_ = {};
    mag_bonded_ = {};
    if (beam_blocked_) {
        (void)machine_.handle(sm::Event::SafetyBeamTripped);  // re-latch
    }
    resolve_position_locked();
}

void VirtualGate::trip_beam() {
    const std::lock_guard<std::mutex> lock(mutex_);
    beam_blocked_ = true;
    // The operator owns reversal on its Safety Close input.
    if (cfg_.profile == sm::ActuatorProfile::Operator && motor_close_) {
        motor_close_ = false;
        motor_open_ = true;
        run_ms_ = 0.0;
    }
    step_locked(sm::Event::SafetyBeamTripped);
}

void VirtualGate::clear_beam() {
    const std::lock_guard<std::mutex> lock(mutex_);
    beam_blocked_ = false;
    step_locked(sm::Event::SafetyBeamCleared);
}

void VirtualGate::rte() {
    const std::lock_guard<std::mutex> lock(mutex_);
    apply_locked(machine_.handle(sm::Event::CommandOpen, sm::OpenOrigin::Rte));
}

bool VirtualGate::latch_open() {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto out = machine_.handle(sm::Event::CommandOpen);
    apply_locked(out);
    const auto s = machine_.state();
    if (s == sm::State::Closed || s == sm::State::Lockdown || s == sm::State::Faulted) {
        return false;
    }
    open_latched_ = true;
    autoclose_deadline_ = 0;
    return true;
}

bool VirtualGate::latch_close() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!locked_gate()) {
        return false;  // legacy gates have no lockdown
    }
    open_latched_ = false;
    step_locked(sm::Event::LockdownOn);
    return true;
}

void VirtualGate::release_latch() {
    const std::lock_guard<std::mutex> lock(mutex_);
    open_latched_ = false;
    step_locked(sm::Event::LockdownOff);
    if (machine_.state() == sm::State::Open && cfg_.auto_close_ms > 0) {
        autoclose_deadline_ = now_ms_ + cfg_.auto_close_ms;
    }
}

void VirtualGate::jam_bolt(std::size_t index, bool jammed) {
    const std::lock_guard<std::mutex> lock(mutex_);
    bolt_jam_.at(index) = jammed;
}

void VirtualGate::stick_maglock(std::size_t index, bool stuck) {
    const std::lock_guard<std::mutex> lock(mutex_);
    mag_stuck_.at(index) = stuck;
}

void VirtualGate::set_key_override(bool active) {
    const std::lock_guard<std::mutex> lock(mutex_);
    key_active_ = active;
    sequencer_.abort();
    step_locked(active ? sm::Event::KeyOverrideOn : sm::Event::KeyOverrideOff);
}

void VirtualGate::set_battery(sm::BatteryLevel level) {
    const std::lock_guard<std::mutex> lock(mutex_);
    machine_.set_battery(level);
}

void VirtualGate::begin_verify(sm::GateType actual) {
    const std::lock_guard<std::mutex> lock(mutex_);
    actual_type_ = actual;
    step_locked(sm::Event::BeginVerify);
}

void VirtualGate::force_motor_open() {
    const std::lock_guard<std::mutex> lock(mutex_);
    forced_motor_ = true;
    motor_open_ = true;
}

bool VirtualGate::interlock_ok() const {
    if (!locked_gate()) {
        return true;
    }
    if (!permit_ || key_active_) {
        return false;
    }
    for (const double b : bolt_ms_) {
        if (b < cfg_.bolt_transit_ms) {
            return false;
        }
    }
    for (const bool m : mag_bonded_) {
        if (m) {
            return false;
        }
    }
    return true;
}

// Dual leaf: leaf A leads on open and leaf B leads on close, so the
// overlap at the meeting edge seats correctly. The delay is the
// operator's own setting, never a firmware command.
void VirtualGate::move_leaves(double dt_ms, int direction) {
    run_ms_ += dt_ms;
    const double delay = cfg_.leaf_delay_ms;
    const double travel = cfg_.travel_ms;
    double& leader = direction > 0 ? position_ms_ : position_b_ms_;
    double& follower = direction > 0 ? position_b_ms_ : position_ms_;
    const auto step = [&](double& p) {
        p += direction * dt_ms;
        p = p < 0.0 ? 0.0 : (p > travel ? travel : p);
    };
    if (!dual()) {
        step(position_ms_);
        return;
    }
    step(leader);
    if (run_ms_ >= delay) {
        step(follower);
    }
}

sm::LockFeedback VirtualGate::feedback() const {
    sm::LockFeedback fb;
    for (std::size_t i = 0; i < bolt_ms_.size(); ++i) {
        fb.bolt_extended[i] = bolt_ms_[i] <= 0.0;
        fb.bolt_retracted[i] = bolt_ms_[i] >= cfg_.bolt_transit_ms;
    }
    fb.mag_bonded = mag_bonded_;
    return fb;
}

void VirtualGate::advance_locks(std::uint32_t dt_ms) {
    for (std::size_t i = 0; i < bolt_ms_.size(); ++i) {
        const bool pull = sequencer_.bolt_drive() && !bolt_jam_[i];
        bolt_ms_[i] += pull ? double(dt_ms) : -double(dt_ms);
        const double max = cfg_.bolt_transit_ms;
        bolt_ms_[i] = bolt_ms_[i] < 0.0 ? 0.0 : (bolt_ms_[i] > max ? max : bolt_ms_[i]);
    }
    const std::size_t mags = cfg_.lock_layout == sm::LockLayout::Five ? 3U : 2U;
    for (std::size_t i = 0; i < mags; ++i) {
        // The key override cuts maglock power by wire.
        const bool want = (sequencer_.maglock_drive() && !key_active_) || mag_stuck_[i];
        if (want == mag_bonded_[i]) {
            mag_ms_[i] = 0.0;
        } else {
            mag_ms_[i] += dt_ms;
            if (mag_ms_[i] >= cfg_.mag_transit_ms) {
                mag_bonded_[i] = want;
                mag_ms_[i] = 0.0;
            }
        }
    }
    switch (sequencer_.tick(now_ms_, feedback())) {
        case sm::LockSequencer::Result::Released:
            step_locked(sm::Event::LocksReleased);
            break;
        case sm::LockSequencer::Result::Engaged:
            step_locked(sm::Event::LocksEngaged);
            break;
        case sm::LockSequencer::Result::Failed:
            step_locked(sequencer_.fault() == sm::LockFault::Feedback ? sm::Event::LockFeedbackFault
                                                                      : sm::Event::LockFailed);
            break;
        case sm::LockSequencer::Result::None:
            break;
    }
}

void VirtualGate::advance(std::uint32_t dt_ms) {
    const std::lock_guard<std::mutex> lock(mutex_);
    now_ms_ += dt_ms;
    record(tracker_.on_tick(static_cast<std::uint32_t>(now_ms_)));

    if (locked_gate()) {
        advance_locks(dt_ms);
    }

    // The interlock is independent of the firmware model: no permit,
    // or any lock still holding, and the operator cannot move at all.
    if (interlock_ok()) {
        const bool was_closed = at_closed();
        if (motor_open_) {
            move_leaves(dt_ms, 1);
            if (was_closed && !at_closed() && locked_gate()) {
                step_locked(sm::Event::LimitClosedReleased);
            }
            if (at_open() && !forced_motor_) {
                step_locked(sm::Event::LimitOpenHit);
            }
        } else if (motor_close_) {
            move_leaves(dt_ms, -1);
            if (at_closed()) {
                step_locked(sm::Event::LimitClosedHit);
            }
        }
    }

    if (verify_deadline_ != 0 && now_ms_ >= verify_deadline_) {
        verify_deadline_ = 0;
        motor_open_ = false;
        step_locked(actual_type_ == cfg_.gate_type ? sm::Event::VerifyPassed
                                                   : sm::Event::VerifyFailed);
    }
    if (calibrate_deadline_ != 0 && now_ms_ >= calibrate_deadline_) {
        calibrate_deadline_ = 0;
        step_locked(sm::Event::CalibrateDone);
    }
    if (bolt_rest_deadline_ != 0 && now_ms_ >= bolt_rest_deadline_) {
        bolt_rest_deadline_ = 0;
        sequencer_.rest_bolts();
        step_locked(sm::Event::BoltsRested);
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
    return at_open();
}

bool VirtualGate::limit_closed() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return at_closed();
}

bool VirtualGate::beam_clear() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return !beam_blocked_;
}

double VirtualGate::position_permille_a() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return position_ms_ * 1000.0 / cfg_.travel_ms;
}

double VirtualGate::position_permille_b() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return position_b_ms_ * 1000.0 / cfg_.travel_ms;
}

bool VirtualGate::bolts_extended() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto fb = feedback();
    return fb.bolt_extended[0] && fb.bolt_extended[1];
}

bool VirtualGate::maglocks_bonded() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t mags = cfg_.lock_layout == sm::LockLayout::Five ? 3U : 2U;
    for (std::size_t i = 0; i < mags; ++i) {
        if (!mag_bonded_[i]) {
            return false;
        }
    }
    return true;
}

bool VirtualGate::permit() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return permit_;
}

bool VirtualGate::lockdown_latched() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return machine_.lockdown_latched();
}

sm::Reason VirtualGate::reason() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return machine_.reason();
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
    const std::string_view state = sm::to_string(machine_.state());
    std::snprintf(buf, sizeof(buf), "state=%.*s position=%.0f%% beam=%s motor=%s tracker=%s",
                  static_cast<int>(state.size()), state.data(),
                  (position_ms_ / cfg_.travel_ms) * 100.0, beam_blocked_ ? "BLOCKED" : "clear",
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
            case sm::Action::ReleaseLocks:
                sequencer_.start_release(now_ms_);
                break;
            case sm::Action::RetractBolts:
                sequencer_.start_retract(now_ms_);
                break;
            case sm::Action::EngageLocks:
                sequencer_.start_engage(now_ms_);
                break;
            case sm::Action::RestBolts:
                sequencer_.abort();
                sequencer_.rest_bolts();
                break;
            case sm::Action::SetPermit:
                permit_ = true;
                break;
            case sm::Action::ClearPermit:
                permit_ = false;
                break;
            case sm::Action::RunVerifyPulse:
                motor_open_ = true;
                run_ms_ = 0.0;
                verify_deadline_ = now_ms_ + cfg_.verify_pulse_ms;
                break;
            case sm::Action::RunCalibration:
                calibrate_deadline_ = now_ms_ + 1;
                break;
            default:
                break;  // LEDs / telemetry have no physics
        }
        if (out.actions[i] == sm::Action::DriveMotorOpen ||
            out.actions[i] == sm::Action::DriveMotorClose) {
            run_ms_ = 0.0;
        }
    }

    if (out.transitioned) {
        const bool moving =
            out.new_state == sm::State::Opening || out.new_state == sm::State::Closing;
        watchdog_deadline_ = moving ? now_ms_ + cfg_.motor_timeout_ms : 0;
        // Under lockdown the only open is an exit, and it closes on the
        // offline timer whatever the configured auto-close says.
        const std::uint32_t close_after =
            machine_.lockdown_latched() ? cfg_.offline_close_ms : cfg_.auto_close_ms;
        autoclose_deadline_ =
            (out.new_state == sm::State::Open && close_after > 0 && !open_latched_)
                ? now_ms_ + close_after
                : 0;
        const bool holds_bolts = out.new_state == sm::State::Open ||
                                 out.new_state == sm::State::StoppedOpen ||
                                 out.new_state == sm::State::StoppedClose;
        if (!holds_bolts) {
            bolt_rest_deadline_ = 0;
        } else if (locked_gate() && bolt_rest_deadline_ == 0 && cfg_.bolt_hold_open_ms > 0) {
            bolt_rest_deadline_ = now_ms_ + cfg_.bolt_hold_open_ms;  // cumulative across a stop
        }
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
    if (at_closed()) {
        const auto fb = feedback();
        bool proven = fb.bolt_extended[0] && fb.bolt_extended[1];
        const std::size_t mags = cfg_.lock_layout == sm::LockLayout::Five ? 3U : 2U;
        for (std::size_t i = 0; i < mags; ++i) {
            proven = proven && mag_bonded_[i];
        }
        apply_locked(machine_.init_closed(proven));
        if (machine_.state() == sm::State::ClosedInsecure) {
            sequencer_.start_engage(now_ms_);  // the relock attempt
        }
    } else if (at_open()) {
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
