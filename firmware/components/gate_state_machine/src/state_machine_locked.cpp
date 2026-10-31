// state_machine_locked.cpp — the lock-sequenced transition table.
//
// Implements docs/design/gate-system-v2.md section 8.4 for any layout
// other than LockLayout::None. The one invariant everything here serves:
// no motion action leaves this file unless locks_free_ is set, and only
// a LocksReleased event in a state that asked for it can set it.

#include <cassert>

#include "gate_state_machine/state_machine.hpp"

namespace gate::state_machine {

namespace {

void push(Outputs& o, Action a) noexcept {
    assert(o.count < kMaxActionsPerStep);
    if (o.count < kMaxActionsPerStep) {
        o.actions[o.count++] = a;
    }
}

enum class FaultClass : std::uint8_t { NonLock, Lock, Hardware };

// Spec section 7.3, fault clearing.
FaultClass classify(Reason r) noexcept {
    switch (r) {
        case Reason::PermitFault:
        case Reason::LimitSwitchConflict:
            return FaultClass::Hardware;
        case Reason::LockJam:
        case Reason::LockFeedback:
            return FaultClass::Lock;
        default:
            return FaultClass::NonLock;
    }
}

bool is_stopped(State s) noexcept {
    return s == State::StoppedOpen || s == State::StoppedClose;
}

bool in_procedure(State s) noexcept {
    return s == State::Verifying || s == State::Calibrating;
}

void refuse(Outputs& o, Reason r) noexcept {
    o.reason = r;
    push(o, Action::EmitTelemetryStateChanged);
}

}  // namespace

void StateMachine::enter(Outputs& o, State s, Reason r) noexcept {
    state_ = s;
    reason_ = r;
    o.new_state = s;
    o.reason = r;
    o.transitioned = true;
}

void StateMachine::enter_locking(Outputs& o, Reason r) noexcept {
    enter(o, State::Locking, r);
    locks_free_ = false;
    push(o, Action::ClearPermit);
    push(o, Action::EngageLocks);
    push(o, Action::SetLedClosing);
    push(o, Action::EmitTelemetryStateChanged);
}

void StateMachine::enter_fault(Outputs& o, Reason r, bool stop_motor) noexcept {
    enter(o, State::Faulted, r);
    locks_free_ = false;
    if (stop_motor) {
        push(o, Action::StopMotor);
    }
    push(o, Action::ClearPermit);
    push(o, Action::RestBolts);
    push(o, Action::RaiseAlert);
    push(o, Action::SetLedFault);
    push(o, Action::EmitTelemetryStateChanged);
}

// From Open, or Stopped with rested bolts, the bolts are re-proven
// first. Stopped with bolts still held goes straight to Closing.
void StateMachine::begin_close(Outputs& o) noexcept {
    close_pending_ = false;
    if (is_stopped(state_) && locks_free_) {
        enter(o, State::Closing, Reason::None);
        push(o, Action::SetPermit);
        push(o, Action::DriveMotorClose);
    } else {
        pre_close_from_ = state_;
        enter(o, State::PreClose, Reason::None);
        push(o, Action::RetractBolts);
    }
    push(o, Action::SetLedClosing);
    push(o, Action::EmitTelemetryStateChanged);
}

void StateMachine::try_lockdown_close(Outputs& o) noexcept {
    if (state_ != State::Open && !is_stopped(state_)) {
        close_pending_ = true;  // Opening: close on arrival at Open
    } else if (!beam_clear_ || key_active_) {
        close_pending_ = true;  // never close into a blocked beam
    } else {
        begin_close(o);
    }
}

// Leaves Verifying or Calibrating: back to the closed limit, then lock.
void StateMachine::finish_procedure(Outputs& o, Reason r, bool alert) noexcept {
    if (at_closed_) {
        push(o, Action::StopMotor);
        enter_locking(o, r);
    } else if (beam_clear_ && !key_active_) {
        enter(o, State::Closing, r);
        locks_free_ = true;  // the procedure ran with locks released
        push(o, Action::StopMotor);
        push(o, Action::SetPermit);
        push(o, Action::DriveMotorClose);
        push(o, Action::SetLedClosing);
        push(o, Action::EmitTelemetryStateChanged);
    } else {
        enter(o, State::StoppedClose, r);
        locks_free_ = true;
        close_pending_ = lockdown_;
        push(o, Action::StopMotor);
        push(o, Action::SetLedStopped);
        push(o, Action::EmitTelemetryStateChanged);
    }
    if (alert) {
        push(o, Action::RaiseAlert);
    }
}

// Unlocking has moved nothing, so "back" is relock at the limit, or a
// plain stop if it began mid-travel. Bolts then rest on the hold timer.
void StateMachine::abort_unlock(Outputs& o, Reason r) noexcept {
    if (at_closed_) {
        enter_locking(o, r);
    } else {
        enter(o, State::StoppedOpen, r);
        push(o, Action::SetLedStopped);
        push(o, Action::EmitTelemetryStateChanged);
    }
}

void StateMachine::on_open(Outputs& o, OpenOrigin origin) noexcept {
    const bool rte = origin == OpenOrigin::Rte;
    switch (state_) {
        case State::Opening:
        case State::Open:
        case State::Unlocking:
            rte_cycle_ = rte_cycle_ || rte;
            return;  // already serving an open
        case State::Initializing:
            refuse(o, Reason::Busy);
            return;
        case State::Verifying:
        case State::Calibrating:
            if (!rte) {
                refuse(o, Reason::Busy);
                return;
            }
            break;
        case State::Faulted:
            if (!rte) {
                refuse(o, reason_);
                return;
            }
            if (classify(reason_) == FaultClass::Hardware) {
                // Logged only; the way out is the mechanical release.
                refuse(o, reason_);
                push(o, Action::RaiseAlert);
                return;
            }
            break;
        default:
            break;
    }

    if (key_active_) {
        refuse(o, Reason::KeyOverride);
        return;
    }
    if (rte ? battery_ == BatteryLevel::BelowCutoff : battery_ != BatteryLevel::Ok) {
        refuse(o, Reason::BatteryReserve);
        return;
    }
    if (!rte && lockdown_) {
        refuse(o, Reason::Lockdown);
        return;
    }

    rte_cycle_ = rte;
    close_pending_ = false;
    const Reason r = (rte && lockdown_) ? Reason::Lockdown : Reason::None;

    if (state_ == State::PreClose && pre_close_from_ == State::Open) {
        enter(o, State::Open, r);  // bolts stay retracted, timers re-arm
        push(o, Action::SetLedOpen);
    } else if (state_ == State::Closing) {
        enter(o, State::Opening, r);  // FRX re-opens a closing gate
        push(o, Action::DriveMotorOpen);
        push(o, Action::SetLedOpening);
    } else if (locks_free_ && state_ != State::PreClose) {
        enter(o, State::Opening, r);
        push(o, Action::SetPermit);
        push(o, Action::DriveMotorOpen);
        push(o, Action::SetLedOpening);
    } else {
        enter(o, State::Unlocking, r);
        push(o, Action::ReleaseLocks);
        push(o, Action::SetLedOpening);
    }
    push(o, Action::EmitTelemetryStateChanged);
    if (r == Reason::Lockdown) {
        push(o, Action::RaiseAlert);
    }
}

void StateMachine::on_close(Outputs& o) noexcept {
    if (state_ != State::Open && !is_stopped(state_)) {
        return;
    }
    if (key_active_) {
        refuse(o, Reason::KeyOverride);
    } else if (!beam_clear_) {
        close_pending_ = lockdown_;
        refuse(o, Reason::SafetyBeamObstacle);
    } else {
        begin_close(o);
    }
}

void StateMachine::on_stop(Outputs& o) noexcept {
    switch (state_) {
        case State::Opening:
        case State::Closing:
            enter(o, state_ == State::Opening ? State::StoppedOpen : State::StoppedClose,
                  Reason::OperatorStop);
            push(o, Action::StopMotor);
            push(o, Action::SetLedStopped);
            push(o, Action::EmitTelemetryStateChanged);
            break;
        case State::Unlocking:
            abort_unlock(o, Reason::OperatorStop);
            break;
        case State::PreClose:
            enter(o, pre_close_from_, Reason::OperatorStop);
            push(o, pre_close_from_ == State::Open ? Action::SetLedOpen : Action::SetLedStopped);
            push(o, Action::EmitTelemetryStateChanged);
            break;
        case State::Verifying:
        case State::Calibrating:
            finish_procedure(o, Reason::OperatorStop, false);
            break;
        default:
            break;
    }
}

void StateMachine::on_key_on(Outputs& o) noexcept {
    key_active_ = true;
    switch (state_) {
        case State::Closed:
        case State::ClosedInsecure:
        case State::Lockdown:
        case State::Locking:
            enter(o, State::ClosedInsecure, Reason::KeyOverride);
            locks_free_ = false;
            push(o, Action::ClearPermit);
            push(o, Action::RestBolts);
            push(o, Action::RaiseAlert);
            push(o, Action::SetLedClosed);
            push(o, Action::EmitTelemetryStateChanged);
            break;
        case State::Opening:
        case State::Closing:
            enter(o, state_ == State::Opening ? State::StoppedOpen : State::StoppedClose,
                  Reason::KeyOverride);
            locks_free_ = false;
            push(o, Action::StopMotor);
            push(o, Action::ClearPermit);
            push(o, Action::RaiseAlert);
            push(o, Action::SetLedStopped);
            push(o, Action::EmitTelemetryStateChanged);
            break;
        case State::Unlocking:
        case State::Verifying:
        case State::Calibrating: {
            State to = State::StoppedClose;
            if (at_closed_) {
                to = State::ClosedInsecure;
            }
            const bool was_unlocking = state_ == State::Unlocking;
            enter(o, to, Reason::KeyOverride);
            locks_free_ = false;
            if (!was_unlocking) {
                push(o, Action::StopMotor);
            }
            push(o, Action::ClearPermit);
            push(o, Action::RestBolts);
            push(o, Action::RaiseAlert);
            push(o, Action::EmitTelemetryStateChanged);
            break;
        }
        case State::PreClose:
            enter(o, pre_close_from_, Reason::KeyOverride);
            locks_free_ = false;
            push(o, Action::ClearPermit);
            push(o, Action::RestBolts);
            push(o, Action::RaiseAlert);
            push(o, Action::EmitTelemetryStateChanged);
            break;
        default:
            // Open, Stopped, Faulted: position unchanged, motion refused.
            // The locks are no longer ours, so any later close re-proves.
            locks_free_ = false;
            o.reason = Reason::KeyOverride;
            push(o, Action::ClearPermit);
            push(o, Action::RaiseAlert);
            push(o, Action::EmitTelemetryStateChanged);
            break;
    }
}

void StateMachine::on_lockdown_on(Outputs& o) noexcept {
    if (!lockdown_) {
        push(o, Action::PersistLockdown);
    }
    lockdown_ = true;
    switch (state_) {
        case State::Closed:
            enter(o, State::Lockdown, Reason::None);
            push(o, Action::SetLedClosed);
            push(o, Action::EmitTelemetryStateChanged);
            break;
        case State::Unlocking:
            if (!rte_cycle_) {
                abort_unlock(o, Reason::Lockdown);
                if (state_ == State::StoppedOpen) {
                    try_lockdown_close(o);
                }
            }
            break;
        case State::Opening:
        case State::Open:
        case State::StoppedOpen:
        case State::StoppedClose:
            // An exit in progress finishes: the driver's offline_close_s
            // timer closes it afterwards.
            if (!rte_cycle_) {
                try_lockdown_close(o);
            }
            break;
        case State::Verifying:
        case State::Calibrating:
            finish_procedure(o, Reason::Lockdown, false);
            break;
        default:
            break;  // closing states run on; insecure and faulted just latch
    }
}

// LockFailed and LockFeedbackFault differ only in the reason recorded.
void StateMachine::on_lock_failure(Outputs& o, Reason r) noexcept {
    switch (state_) {
        case State::Unlocking:
        case State::Verifying:
        case State::Calibrating: {
            const Reason fault = r == Reason::LockFeedback ? r : Reason::LockJam;
            // Bolts off, maglocks on: the safe resting state.
            enter_fault(o, fault, false);
            push(o, Action::EngageLocks);
            break;
        }
        case State::PreClose:
            enter(o, pre_close_from_, r);
            locks_free_ = false;
            push(o, Action::RaiseAlert);
            push(o, pre_close_from_ == State::Open ? Action::SetLedOpen : Action::SetLedStopped);
            push(o, Action::EmitTelemetryStateChanged);
            break;
        case State::Locking:
            enter(o, State::ClosedInsecure, r);
            push(o, Action::RaiseAlert);
            push(o, Action::SetLedClosed);
            push(o, Action::EmitTelemetryStateChanged);
            break;
        default:
            break;
    }
}

// The gate left the closed limit with nothing driving it.
void StateMachine::follow_hand_move(Outputs& o, Reason r) noexcept {
    enter(o, State::StoppedOpen, r);
    locks_free_ = false;
    close_pending_ = lockdown_;
    push(o, Action::SetLedStopped);
    push(o, Action::EmitTelemetryStateChanged);
}

void StateMachine::on_insecure(Outputs& o, Reason r) noexcept {
    if (state_ == State::Closed || state_ == State::Lockdown) {
        // With key sense active the same evidence is an audited override.
        const Reason why = key_active_ ? Reason::KeyOverride : r;
        if (!at_closed_) {  // the limit release arrived first
            follow_hand_move(o, why);
            push(o, Action::RaiseAlert);
            return;
        }
        enter(o, State::ClosedInsecure, why);
        push(o, Action::RaiseAlert);
        push(o, Action::SetLedClosed);
        push(o, Action::EmitTelemetryStateChanged);
    }
}

// Events whose effect depends only on the current state.
void StateMachine::on_state_event(Outputs& o, Event ev) noexcept {
    const bool wants_locks = state_ == State::Unlocking || state_ == State::PreClose ||
                             (in_procedure(state_) && !locks_free_);
    const bool moving = state_ == State::Opening || state_ == State::Closing;

    switch (ev) {
        case Event::LocksReleased:
            if (!wants_locks) {
                break;
            }
            locks_free_ = true;
            if (state_ == State::Unlocking) {
                enter(o, State::Opening, reason_);
                push(o, Action::SetPermit);
                push(o, Action::DriveMotorOpen);
                push(o, Action::SetLedOpening);
                push(o, Action::EmitTelemetryStateChanged);
            } else if (state_ == State::PreClose) {
                if (!beam_clear_) {
                    // The beam tripped while the bolts were retracting.
                    enter(o, pre_close_from_, Reason::SafetyBeamObstacle);
                    close_pending_ = lockdown_;
                    push(o, Action::EmitTelemetryStateChanged);
                    break;
                }
                enter(o, State::Closing, Reason::None);
                push(o, Action::SetPermit);
                push(o, Action::DriveMotorClose);
                push(o, Action::SetLedClosing);
                push(o, Action::EmitTelemetryStateChanged);
            } else {
                push(o, Action::SetPermit);
                push(o,
                     state_ == State::Verifying ? Action::RunVerifyPulse : Action::RunCalibration);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case Event::LocksEngaged:
            if (state_ == State::Locking || state_ == State::ClosedInsecure) {
                rte_cycle_ = false;
                enter(o, lockdown_ ? State::Lockdown : State::Closed, Reason::None);
                push(o, Action::SetLedClosed);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case Event::LimitOpenHit:
            if (state_ == State::Opening) {
                enter(o, State::Open, Reason::None);
                push(o, Action::StopMotor);
                push(o, Action::SetLedOpen);
                push(o, Action::EmitTelemetryStateChanged);
                // A reversal can bring a lockdown close back here.
                if (lockdown_ && !rte_cycle_) {
                    try_lockdown_close(o);
                }
            }
            break;

        case Event::LimitClosedHit:
            if (state_ == State::Closing) {
                push(o, Action::StopMotor);
                enter_locking(o, Reason::None);
            } else if (is_stopped(state_)) {
                // Pushed back by hand: shut, locks unproven.
                enter(o, State::ClosedInsecure, reason_);
                locks_free_ = false;
                push(o, Action::SetLedClosed);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case Event::LimitClosedReleased:
            if (state_ == State::ClosedInsecure) {
                follow_hand_move(o, reason_);
            }
            break;

        case Event::SafetyBeamTripped:
            if (state_ == State::Closing) {
                if (cfg_.profile == ActuatorProfile::Operator) {
                    // No command: the operator reverses by itself.
                    enter(o, State::Opening, Reason::SafetyBeamObstacle);
                    push(o, Action::SetLedOpening);
                } else {
                    enter(o, State::StoppedClose, Reason::SafetyBeamObstacle);
                    push(o, Action::StopMotor);
                    push(o, Action::SetLedStopped);
                }
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case Event::SafetyBeamCleared:
            if (close_pending_ && lockdown_ && !rte_cycle_) {
                try_lockdown_close(o);
            }
            break;

        case Event::OperatorNoResponse:
        case Event::OperatorDirectionWrong:
            if (moving) {
                enter_fault(o, Reason::OperatorFault, true);
            }
            break;

        case Event::MotorTimeout:
            if (moving) {
                enter_fault(o, Reason::MotorTimeout, true);
            }
            break;

        case Event::OperatorCollision:
            if (moving) {
                o.reason = Reason::OperatorCollision;
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case Event::OperatorReversed:
            if (moving) {
                const bool to_open = state_ == State::Closing;
                enter(o, to_open ? State::Opening : State::Closing, Reason::OperatorCollision);
                push(o, to_open ? Action::SetLedOpening : Action::SetLedClosing);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case Event::CurrentOverrun:
            if (moving) {
                o.reason = Reason::CurrentAnomaly;  // never a motion command
                push(o, Action::RaiseAlert);
            }
            break;

        case Event::BoltsRested:
            if (state_ == State::Open || is_stopped(state_)) {
                locks_free_ = false;
                push(o, Action::ClearPermit);
            }
            break;

        case Event::BatteryCritical:
            if (state_ == State::Open || is_stopped(state_)) {
                if (beam_clear_ && !key_active_) {
                    begin_close(o);
                } else {
                    // The driver retries every 10 s.
                    refuse(o, key_active_ ? Reason::KeyOverride : Reason::SafetyBeamObstacle);
                }
            }
            break;

        case Event::BeginVerify:
            if (state_ == State::Closed && beam_clear_ && !key_active_) {
                enter(o, State::Verifying, Reason::None);
                push(o, Action::ReleaseLocks);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case Event::BeginCalibrate:
            if (state_ == State::Closed && beam_clear_ && !key_active_) {
                enter(o, State::Calibrating, Reason::None);
                push(o, Action::ReleaseLocks);
                push(o, Action::EmitTelemetryStateChanged);
            } else if (state_ == State::Verifying && locks_free_) {
                enter(o, State::Calibrating, Reason::None);
                push(o, Action::RunCalibration);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case Event::VerifyPassed:
            if (state_ == State::Verifying) {
                finish_procedure(o, Reason::None, false);
            }
            break;

        case Event::VerifyFailed:
            if (state_ == State::Verifying) {
                finish_procedure(o, Reason::GateTypeMismatch, true);
            }
            break;

        case Event::CalibrateDone:
            if (state_ == State::Calibrating) {
                finish_procedure(o, Reason::None, false);
            }
            break;

        case Event::CalibrateFailed:
            if (state_ == State::Calibrating) {
                finish_procedure(o, Reason::CalibrationFailed, true);
            }
            break;

        default:
            break;
    }
}

Outputs StateMachine::handle_locked(Event ev, OpenOrigin origin) noexcept {
    Outputs o{};
    o.new_state = state_;

    switch (ev) {
        case Event::SafetyBeamTripped:
            beam_clear_ = false;
            break;
        case Event::SafetyBeamCleared:
            beam_clear_ = true;
            break;
        case Event::LimitClosedHit:
            at_closed_ = true;
            break;
        case Event::LimitClosedReleased:
            at_closed_ = false;
            break;
        default:
            break;
    }

    switch (ev) {
        case Event::CommandOpen:
            on_open(o, origin);
            break;
        case Event::CommandClose:
            on_close(o);
            break;
        case Event::CommandStop:
            on_stop(o);
            break;
        case Event::KeyOverrideOn:
            on_key_on(o);
            break;
        case Event::KeyOverrideOff:
            key_active_ = false;
            if (state_ == State::ClosedInsecure && at_closed_) {
                enter_locking(o, Reason::None);  // the driver restarts its relock count
            } else if (close_pending_ && lockdown_ && !rte_cycle_) {
                try_lockdown_close(o);
            } else {
                push(o, Action::EmitTelemetryStateChanged);  // no auto-resume
            }
            break;
        case Event::LockdownOn:
            on_lockdown_on(o);
            break;
        case Event::LockdownOff:
            if (lockdown_) {
                push(o, Action::PersistLockdown);
            }
            lockdown_ = false;
            close_pending_ = false;
            if (state_ == State::Lockdown) {
                enter(o, State::Closed, Reason::None);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;
        case Event::LockFailed:
            on_lock_failure(o, state_ == State::Locking ? Reason::LockIncomplete : Reason::LockJam);
            break;
        case Event::LockFeedbackFault:
            on_lock_failure(o, Reason::LockFeedback);
            break;
        case Event::LockDropped:
            on_insecure(o, Reason::LockIncomplete);
            break;
        case Event::GateForced:
            on_insecure(o, Reason::ForcedEntry);
            break;
        case Event::PermitFault:
            if (state_ != State::Initializing) {
                enter_fault(o, Reason::PermitFault, true);
            }
            break;
        case Event::FaultCleared:
            if (state_ == State::Faulted) {
                enter(o, State::Initializing, Reason::None);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;
        case Event::LocalReset:
            if (state_ == State::Faulted) {
                if (classify(reason_) == FaultClass::Hardware) {
                    refuse(o, reason_);
                } else {
                    enter(o, State::Initializing, Reason::None);
                    push(o, Action::EmitTelemetryStateChanged);
                }
            }
            break;
        default:
            on_state_event(o, ev);
            break;
    }
    return o;
}

}  // namespace gate::state_machine
