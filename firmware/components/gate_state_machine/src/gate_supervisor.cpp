// gate_supervisor.cpp — see the header.

#include "gate_state_machine/gate_supervisor.hpp"

namespace gate::state_machine {

namespace {

void add(GateSupervisor::Events& e, Event ev) noexcept {
    if (e.count < e.items.size()) {
        e.items[e.count++] = ev;
    }
}

}  // namespace

GateSupervisor::GateSupervisor(Config cfg, LockIo& locks, GateActuator& actuator) noexcept
    : cfg_(cfg), locks_(locks), actuator_(actuator), sequencer_(cfg.layout, cfg.timing) {}

void GateSupervisor::observe(Event ev, std::uint64_t now_ms) noexcept {
    if (ev == Event::SafetyBeamTripped || ev == Event::OperatorCollision) {
        quiet_until_ = now_ms + cfg_.beam_quiet_ms;
        guard_until_ = now_ms + cfg_.reversal_guard_ms;
    } else if (ev == Event::OperatorReversed) {
        guard_until_ = now_ms + cfg_.reversal_guard_ms;
    } else if (ev == Event::CommandStop || ev == Event::KeyOverrideOn || ev == Event::PermitFault) {
        human_stop_ = true;
    }
    if (ev == Event::KeyOverrideOn) {
        key_active_ = true;
    } else if (ev == Event::KeyOverrideOff) {
        key_active_ = false;
    }
}

void GateSupervisor::command(Motion m, std::uint64_t now_ms) noexcept {
    motion_ = m;
    started_ = false;
    retried_ = false;
    decoder_.forget_motion();
    start_deadline_ = now_ms + cfg_.op_start_timeout_ms;
    if (m == Motion::Open) {
        actuator_.open(now_ms);
    } else {
        actuator_.close(now_ms);
    }
}

// Stopping must not interrupt the operator's own reversal; a person
// pressing stop always wins.
void GateSupervisor::stop(std::uint64_t now_ms) noexcept {
    const bool moving = motion_ != Motion::None;
    motion_ = Motion::None;
    if (guarded(now_ms)) {
        deferred_stop_ = true;
        return;
    }
    actuator_.stop();
    if (moving && operator_profile() && permit_wanted_) {
        locks_.set_permit(false);
        rearm_at_ = now_ms + cfg_.permit_rearm_ms;
    }
}

void GateSupervisor::drive(Motion m, std::uint64_t now_ms) noexcept {
    if (rested_) {
        // BoltsRested lost the race to this command: prove them first.
        pending_ = m;
        permit_wanted_ = false;
        permit_check_at_ = 0;
        locks_.set_permit(false);
        sequencer_.start_retract(now_ms);
        return;
    }
    command(m, now_ms);
}

void GateSupervisor::clear_permit(std::uint64_t now_ms) noexcept {
    deferred_clear_ = false;
    permit_wanted_ = false;
    rearm_at_ = 0;
    locks_.set_permit(false);
    permit_check_at_ = now_ms + cfg_.permit_check_ms;
}

void GateSupervisor::rest_bolts() noexcept {
    deferred_rest_ = false;
    sequencer_.abort();
    sequencer_.rest_bolts();
}

void GateSupervisor::apply(const Outputs& out, std::uint64_t now_ms) noexcept {
    for (std::uint8_t i = 0; i < out.count; ++i) {
        switch (out.actions[i]) {
            case Action::DriveMotorOpen:
                drive(Motion::Open, now_ms);
                break;
            case Action::DriveMotorClose:
                drive(Motion::Close, now_ms);
                break;
            case Action::RunVerifyPulse:
                refused_ = Event::VerifyFailed;  // never moves the gate
                break;
            case Action::RunCalibration:
                refused_ = Event::CalibrateFailed;
                break;
            case Action::StopMotor:
                stop(now_ms);
                break;
            case Action::ReleaseLocks:
                deferred_rest_ = false;
                sequencer_.start_release(now_ms);
                break;
            case Action::RetractBolts:
                deferred_rest_ = false;
                sequencer_.start_retract(now_ms);
                break;
            case Action::EngageLocks:
                deferred_rest_ = false;
                sequencer_.start_engage(now_ms);
                break;
            case Action::RestBolts:
                if (guarded(now_ms)) {
                    deferred_rest_ = true;
                } else {
                    rest_bolts();
                }
                break;
            case Action::SetPermit:
                deferred_clear_ = false;
                permit_wanted_ = true;
                rearm_at_ = 0;
                locks_.set_permit(true);
                permit_check_at_ = now_ms + cfg_.permit_check_ms;
                break;
            case Action::ClearPermit:
                if (guarded(now_ms)) {
                    deferred_clear_ = true;
                } else {
                    clear_permit(now_ms);
                }
                break;
            default:
                break;  // LEDs, telemetry, alerts stay with the controller
        }
    }
    human_stop_ = false;
    if (out.transitioned) {
        track_state(out, now_ms);
    }
    locks_.drive(sequencer_.bolt_drive(), sequencer_.maglock_drive());
}

void GateSupervisor::track_state(const Outputs& out, std::uint64_t now_ms) noexcept {
    const State s = out.new_state;
    if (s == State::Opening || s == State::Closing) {
        // A followed reversal changes direction with no command.
        const Motion m = s == State::Opening ? Motion::Open : Motion::Close;
        if (motion_ != Motion::None && motion_ != m) {
            motion_ = m;
            decoder_.forget_motion();  // the old flash must not read as a reversal
        }
    }
    const bool holds = s == State::Open || s == State::StoppedOpen || s == State::StoppedClose;
    if (!holds) {
        hold_until_ = 0;
    } else if (hold_until_ == 0) {
        hold_until_ = now_ms + cfg_.bolt_hold_open_ms;
    }
    if (s == State::ClosedInsecure) {
        if (state_ != State::ClosedInsecure && state_ != State::Locking) {
            relocks_ = 0;
        }
        relock_at_ = now_ms + cfg_.relock_retry_ms;
    } else if (s != State::Locking) {
        relock_at_ = 0;
    }
    if (s == State::Locking && state_ == State::ClosedInsecure) {
        relocks_ = 0;  // key release restarts the count
    }
    state_ = s;
}

// Direction check, spec 5.3. Only inside the start window, and never
// just after a beam trip or collision, when the operator owns direction.
void GateSupervisor::check_operator(Events& e, std::uint64_t now_ms) noexcept {
    if (!operator_profile() || motion_ == Motion::None) {
        return;
    }
    const OperatorStatus st = decoder_.status();
    const bool up = st == OperatorStatus::Opening;
    const bool down = st == OperatorStatus::Closing;
    const bool wanted = motion_ == Motion::Open ? up : down;
    const bool opposite = motion_ == Motion::Open ? down : up;
    if (!started_) {
        if (wanted) {
            started_ = true;
            add(e, Event::OperatorMoving);
        } else if (opposite && now_ms >= quiet_until_) {
            add(e, Event::OperatorDirectionWrong);
            motion_ = Motion::None;
        } else if (now_ms >= start_deadline_) {
            if (retried_) {
                add(e, Event::OperatorNoResponse);
                motion_ = Motion::None;
            } else {
                retried_ = true;
                start_deadline_ = now_ms + cfg_.op_start_timeout_ms;
                if (motion_ == Motion::Open) {
                    actuator_.open(now_ms);
                } else {
                    actuator_.close(now_ms);
                }
            }
        }
    } else if (opposite && now_ms >= quiet_until_) {
        add(e, Event::OperatorReversed);  // follow; never command
        motion_ = motion_ == Motion::Open ? Motion::Close : Motion::Open;
    }
}

GateSupervisor::Events GateSupervisor::tick(std::uint64_t now_ms, bool status_lit) noexcept {
    Events e;
    decoder_.sample(status_lit);
    actuator_.tick(now_ms);

    if (rearm_at_ != 0 && now_ms >= rearm_at_) {
        rearm_at_ = 0;
        locks_.set_permit(permit_wanted_);
        permit_check_at_ = now_ms + cfg_.permit_check_ms;
    }

    if (now_ms >= guard_until_) {
        if (deferred_stop_) {
            deferred_stop_ = false;
            actuator_.stop();
        }
        if (deferred_clear_) {
            clear_permit(now_ms);
        }
        if (deferred_rest_) {
            rest_bolts();
        }
    }
    if (refused_ != Event::CommandStop) {
        add(e, refused_);
        refused_ = Event::CommandStop;
    }

    switch (sequencer_.tick(now_ms, locks_.feedback())) {
        case LockSequencer::Result::Released:
            rested_ = false;
            add(e, Event::LocksReleased);
            if (pending_ != Motion::None) {
                permit_wanted_ = true;
                locks_.set_permit(true);
                permit_check_at_ = now_ms + cfg_.permit_check_ms;
                command(pending_, now_ms);
                pending_ = Motion::None;
            }
            break;
        case LockSequencer::Result::Engaged:
            rested_ = false;
            add(e, Event::LocksEngaged);
            break;
        case LockSequencer::Result::Failed:
            if (pending_ != Motion::None) {
                pending_ = Motion::None;  // moving state: LockFailed is not heard there
                add(e, Event::OperatorNoResponse);
                break;
            }
            add(e, sequencer_.fault() == LockFault::Feedback ? Event::LockFeedbackFault
                                                             : Event::LockFailed);
            break;
        case LockSequencer::Result::None:
            break;
    }
    locks_.drive(sequencer_.bolt_drive(), sequencer_.maglock_drive());

    if (permit_check_at_ != 0 && now_ms >= permit_check_at_) {
        permit_check_at_ = 0;
        if (rearm_at_ == 0 && locks_.permit_readback() != permit_wanted_) {
            add(e, Event::PermitFault);
        }
    }
    if (hold_until_ != 0 && now_ms >= hold_until_ && now_ms >= guard_until_) {
        hold_until_ = ~std::uint64_t{0};  // spent until the gate leaves Open/Stopped
        sequencer_.rest_bolts();
        rested_ = true;
        permit_wanted_ = false;
        locks_.set_permit(false);
        locks_.drive(sequencer_.bolt_drive(), sequencer_.maglock_drive());
        add(e, Event::BoltsRested);
    }
    if (relock_at_ != 0 && now_ms >= relock_at_ && !sequencer_.busy() && !key_active_) {
        relock_at_ = 0;
        if (relocks_ < cfg_.relock_tries) {
            ++relocks_;
            sequencer_.start_engage(now_ms);
        }
    }
    check_operator(e, now_ms);
    return e;
}

bool GateSupervisor::locks_proven() noexcept {
    const LockFeedback fb = locks_.feedback();
    bool ok = fb.bolt_extended[0] && fb.bolt_extended[1] && fb.mag_bonded[0] && fb.mag_bonded[1];
    return ok && (cfg_.layout != LockLayout::Five || fb.mag_bonded[2]);
}

void GateSupervisor::assume_engaged() noexcept {
    sequencer_.assume_engaged();
    locks_.drive(sequencer_.bolt_drive(), sequencer_.maglock_drive());
}

}  // namespace gate::state_machine
