// state_machine.cpp — transition table for the gate.
//
// The implementation deliberately uses a flat switch instead of a
// lookup table: there are 8 × 11 = 88 (state, event) combinations and
// most are "ignore". A switch keeps the legal transitions readable, the
// illegal ones explicit, and the binary small enough that ESP-IDF's
// LTO can fold most of it into a few jumps.

#include "gate_state_machine/state_machine.hpp"

#include <cassert>

namespace gate::state_machine {

namespace {

void push(Outputs& o, Action a) noexcept {
    assert(o.count < kMaxActionsPerStep);
    if (o.count < kMaxActionsPerStep) {
        o.actions[o.count++] = a;
    }
}

}  // namespace

StateMachine::StateMachine(Config cfg) noexcept : cfg_(cfg) {}

// ---------- init helpers -----------------------------------------------------

void StateMachine::restore_lockdown(bool latched) noexcept {
    lockdown_ = locked_mode() && latched;
}

Outputs StateMachine::init_closed(bool locks_proven) noexcept {
    Outputs o{};
    at_closed_ = true;
    locks_free_ = false;
    if (locked_mode()) {
        const State s =
            !locks_proven ? State::ClosedInsecure : (lockdown_ ? State::Lockdown : State::Closed);
        state_ = s;
        reason_ = locks_proven ? Reason::None : Reason::LockIncomplete;
        o.new_state = state_;
        o.reason = reason_;
        o.transitioned = true;
        push(o, Action::StopMotor);
        push(o, Action::SetLedClosed);
        push(o, Action::EmitTelemetryStateChanged);
        return o;
    }
    state_ = State::Closed;
    reason_ = Reason::None;
    o.new_state = state_;
    o.transitioned = true;
    push(o, Action::StopMotor);
    push(o, Action::SetLedClosed);
    push(o, Action::EmitTelemetryStateChanged);
    return o;
}

Outputs StateMachine::init_open() noexcept {
    Outputs o{};
    at_closed_ = false;
    locks_free_ = false;
    // A latched lockdown found open at boot closes on the driver's next
    // CommandClose or the next beam-clear; boot itself moves nothing.
    close_pending_ = lockdown_;
    state_ = State::Open;
    reason_ = Reason::None;
    o.new_state = state_;
    o.transitioned = true;
    push(o, Action::StopMotor);
    push(o, Action::SetLedOpen);
    push(o, Action::EmitTelemetryStateChanged);
    return o;
}

Outputs StateMachine::init_limit_conflict() noexcept {
    Outputs o{};
    at_closed_ = false;
    locks_free_ = false;
    state_ = State::Faulted;
    reason_ = Reason::LimitSwitchConflict;
    o.new_state = state_;
    o.transitioned = true;
    o.reason = reason_;
    push(o, Action::StopMotor);
    push(o, Action::SetLedFault);
    push(o, Action::EmitTelemetryStateChanged);
    return o;
}

Outputs StateMachine::init_unknown() noexcept {
    Outputs o{};
    at_closed_ = false;
    locks_free_ = false;
    state_ = State::Faulted;
    // The legacy reason is kept for deployed dashboards; lock-sequenced
    // mode separates "mid-travel" from a both-limits wiring fault.
    reason_ = locked_mode() ? Reason::PositionUnknown : Reason::LimitSwitchConflict;
    o.new_state = state_;
    o.transitioned = true;
    o.reason = reason_;
    push(o, Action::StopMotor);
    push(o, Action::SetLedFault);
    push(o, Action::EmitTelemetryStateChanged);
    return o;
}

// ---------- main transition logic --------------------------------------------

Outputs StateMachine::handle(Event ev, OpenOrigin origin) noexcept {
    if (locked_mode()) {
        return handle_locked(ev, origin);
    }
    Outputs o{};
    o.new_state = state_;

    // Beam latch — track regardless of state so a CommandClose can be
    // rejected the moment it arrives if the beam is still broken.
    if (ev == Event::SafetyBeamTripped) {
        beam_clear_ = false;
    } else if (ev == Event::SafetyBeamCleared) {
        beam_clear_ = true;
    }

    // Operator stop is honored from any active-motion state.
    if (ev == Event::CommandStop) {
        if (state_ == State::Opening) {
            state_ = State::StoppedOpen;
            reason_ = Reason::OperatorStop;
        } else if (state_ == State::Closing) {
            state_ = State::StoppedClose;
            reason_ = Reason::OperatorStop;
        } else {
            return o;  // no-op in steady or already-stopped states
        }
        o.new_state = state_;
        o.reason = reason_;
        o.transitioned = true;
        push(o, Action::StopMotor);
        push(o, Action::SetLedStopped);
        push(o, Action::EmitTelemetryStateChanged);
        return o;
    }

    // Fault reset is honored only from Faulted.
    if (ev == Event::FaultCleared) {
        if (state_ != State::Faulted) {
            return o;
        }
        // Returning to Initializing forces the driver layer to re-read
        // limit switches and call init_*() again before any motion.
        state_ = State::Initializing;
        reason_ = Reason::None;
        o.new_state = state_;
        o.transitioned = true;
        push(o, Action::EmitTelemetryStateChanged);
        return o;
    }

    // Motor runtime watchdog → fault.
    if (ev == Event::MotorTimeout) {
        if (state_ == State::Opening || state_ == State::Closing) {
            state_ = State::Faulted;
            reason_ = Reason::MotorTimeout;
            o.new_state = state_;
            o.reason = reason_;
            o.transitioned = true;
            push(o, Action::StopMotor);
            push(o, Action::SetLedFault);
            push(o, Action::EmitTelemetryStateChanged);
        }
        return o;
    }

    // Per-state transitions.
    switch (state_) {
        case State::Closed:
            if (ev == Event::CommandOpen) {
                state_ = State::Opening;
                o.new_state = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorOpen);
                push(o, Action::SetLedOpening);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case State::Opening:
            if (ev == Event::LimitOpenHit) {
                state_ = State::Open;
                o.new_state = state_;
                o.transitioned = true;
                push(o, Action::StopMotor);
                push(o, Action::SetLedOpen);
                push(o, Action::EmitTelemetryStateChanged);
            }
            // Safety beam during Opening is informational — opening
            // away from an obstruction is allowed, but we still record
            // the latch so the next Close honors it.
            break;

        case State::Open:
            if (ev == Event::CommandClose) {
                if (!beam_clear_) {
                    // Reject the close; remain Open. Surface the
                    // rejection via telemetry but do not transition.
                    push(o, Action::EmitTelemetryStateChanged);
                    o.reason = Reason::SafetyBeamObstacle;
                    return o;
                }
                state_ = State::Closing;
                o.new_state = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorClose);
                push(o, Action::SetLedClosing);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case State::Closing:
            if (ev == Event::LimitClosedHit) {
                state_ = State::Closed;
                o.new_state = state_;
                o.transitioned = true;
                push(o, Action::StopMotor);
                push(o, Action::SetLedClosed);
                push(o, Action::EmitTelemetryStateChanged);
            } else if (ev == Event::SafetyBeamTripped) {
                // Beam break during Closing is the canonical "stop and
                // reverse" scenario — the skeleton stops only. The
                // reverse-to-open behaviour is a config decision that
                // belongs in 4.5.2 where operator policy is available.
                state_ = State::StoppedClose;
                reason_ = Reason::SafetyBeamObstacle;
                o.new_state = state_;
                o.reason = reason_;
                o.transitioned = true;
                push(o, Action::StopMotor);
                push(o, Action::SetLedStopped);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case State::StoppedOpen:
            // Resume mid-open on a fresh CommandOpen.
            if (ev == Event::CommandOpen) {
                state_ = State::Opening;
                reason_ = Reason::None;
                o.new_state = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorOpen);
                push(o, Action::SetLedOpening);
                push(o, Action::EmitTelemetryStateChanged);
            } else if (ev == Event::CommandClose && beam_clear_) {
                state_ = State::Closing;
                reason_ = Reason::None;
                o.new_state = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorClose);
                push(o, Action::SetLedClosing);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case State::StoppedClose:
            // Same resume logic — a Close while the beam is broken
            // stays rejected.
            if (ev == Event::CommandOpen) {
                state_ = State::Opening;
                reason_ = Reason::None;
                o.new_state = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorOpen);
                push(o, Action::SetLedOpening);
                push(o, Action::EmitTelemetryStateChanged);
            } else if (ev == Event::CommandClose && beam_clear_) {
                state_ = State::Closing;
                reason_ = Reason::None;
                o.new_state = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorClose);
                push(o, Action::SetLedClosing);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case State::Unlocking:
        case State::Locking:
        case State::ClosedInsecure:
        case State::PreClose:
        case State::Verifying:
        case State::Calibrating:
        case State::Lockdown:
            break;  // unreachable without a lock layout

        case State::Faulted:
        case State::Initializing:
            // Faulted ignores everything except FaultCleared (handled
            // above). Initializing waits for init_*() — events arriving
            // before init are dropped intentionally.
            break;
    }

    return o;
}

// ---------- string conversions ----------------------------------------------

std::string_view to_string(State s) noexcept {
    switch (s) {
        case State::Initializing:
            return "Initializing";
        case State::Closed:
            return "Closed";
        case State::Opening:
            return "Opening";
        case State::Open:
            return "Open";
        case State::Closing:
            return "Closing";
        case State::StoppedOpen:
            return "StoppedOpen";
        case State::StoppedClose:
            return "StoppedClose";
        case State::Faulted:
            return "Faulted";
        case State::Unlocking:
            return "Unlocking";
        case State::Locking:
            return "Locking";
        case State::ClosedInsecure:
            return "ClosedInsecure";
        case State::PreClose:
            return "PreClose";
        case State::Verifying:
            return "Verifying";
        case State::Calibrating:
            return "Calibrating";
        case State::Lockdown:
            return "Lockdown";
    }
    return "?";
}

std::string_view to_string(Event e) noexcept {
    switch (e) {
        case Event::CommandOpen:
            return "CommandOpen";
        case Event::CommandClose:
            return "CommandClose";
        case Event::CommandStop:
            return "CommandStop";
        case Event::LimitOpenHit:
            return "LimitOpenHit";
        case Event::LimitOpenReleased:
            return "LimitOpenReleased";
        case Event::LimitClosedHit:
            return "LimitClosedHit";
        case Event::LimitClosedReleased:
            return "LimitClosedReleased";
        case Event::SafetyBeamTripped:
            return "SafetyBeamTripped";
        case Event::SafetyBeamCleared:
            return "SafetyBeamCleared";
        case Event::MotorTimeout:
            return "MotorTimeout";
        case Event::FaultCleared:
            return "FaultCleared";
        case Event::LocksReleased:
            return "LocksReleased";
        case Event::LocksEngaged:
            return "LocksEngaged";
        case Event::LockFailed:
            return "LockFailed";
        case Event::LockDropped:
            return "LockDropped";
        case Event::OperatorMoving:
            return "OperatorMoving";
        case Event::OperatorNoResponse:
            return "OperatorNoResponse";
        case Event::OperatorDirectionWrong:
            return "OperatorDirectionWrong";
        case Event::OperatorCollision:
            return "OperatorCollision";
        case Event::OperatorReversed:
            return "OperatorReversed";
        case Event::KeyOverrideOn:
            return "KeyOverrideOn";
        case Event::KeyOverrideOff:
            return "KeyOverrideOff";
        case Event::LocalReset:
            return "LocalReset";
        case Event::CurrentOverrun:
            return "CurrentOverrun";
        case Event::BeginVerify:
            return "BeginVerify";
        case Event::VerifyPassed:
            return "VerifyPassed";
        case Event::VerifyFailed:
            return "VerifyFailed";
        case Event::BeginCalibrate:
            return "BeginCalibrate";
        case Event::CalibrateDone:
            return "CalibrateDone";
        case Event::CalibrateFailed:
            return "CalibrateFailed";
        case Event::LockdownOn:
            return "LockdownOn";
        case Event::LockdownOff:
            return "LockdownOff";
        case Event::GateForced:
            return "GateForced";
        case Event::BatteryCritical:
            return "BatteryCritical";
        case Event::BoltsRested:
            return "BoltsRested";
        case Event::LockFeedbackFault:
            return "LockFeedbackFault";
        case Event::PermitFault:
            return "PermitFault";
    }
    return "?";
}

std::string_view to_string(Action a) noexcept {
    switch (a) {
        case Action::None:
            return "None";
        case Action::DriveMotorOpen:
            return "DriveMotorOpen";
        case Action::DriveMotorClose:
            return "DriveMotorClose";
        case Action::StopMotor:
            return "StopMotor";
        case Action::SetLedClosed:
            return "SetLedClosed";
        case Action::SetLedOpening:
            return "SetLedOpening";
        case Action::SetLedOpen:
            return "SetLedOpen";
        case Action::SetLedClosing:
            return "SetLedClosing";
        case Action::SetLedStopped:
            return "SetLedStopped";
        case Action::SetLedFault:
            return "SetLedFault";
        case Action::EmitTelemetryStateChanged:
            return "EmitTelemetryStateChanged";
        case Action::ReleaseLocks:
            return "ReleaseLocks";
        case Action::EngageLocks:
            return "EngageLocks";
        case Action::RetractBolts:
            return "RetractBolts";
        case Action::SetPermit:
            return "SetPermit";
        case Action::ClearPermit:
            return "ClearPermit";
        case Action::RunVerifyPulse:
            return "RunVerifyPulse";
        case Action::RunCalibration:
            return "RunCalibration";
        case Action::RaiseAlert:
            return "RaiseAlert";
        case Action::PersistLockdown:
            return "PersistLockdown";
        case Action::RestBolts:
            return "RestBolts";
    }
    return "?";
}

std::string_view to_string(Reason r) noexcept {
    switch (r) {
        case Reason::None:
            return "None";
        case Reason::OperatorStop:
            return "OperatorStop";
        case Reason::SafetyBeamObstacle:
            return "SafetyBeamObstacle";
        case Reason::MotorTimeout:
            return "MotorTimeout";
        case Reason::LimitSwitchConflict:
            return "LimitSwitchConflict";
        case Reason::UnexpectedEvent:
            return "UnexpectedEvent";
        case Reason::LockJam:
            return "LockJam";
        case Reason::LockIncomplete:
            return "LockIncomplete";
        case Reason::LockFeedback:
            return "LockFeedback";
        case Reason::OperatorFault:
            return "OperatorFault";
        case Reason::OperatorCollision:
            return "OperatorCollision";
        case Reason::GateTypeMismatch:
            return "GateTypeMismatch";
        case Reason::CalibrationFailed:
            return "CalibrationFailed";
        case Reason::BatteryReserve:
            return "BatteryReserve";
        case Reason::ForcedEntry:
            return "ForcedEntry";
        case Reason::CurrentAnomaly:
            return "CurrentAnomaly";
        case Reason::Lockdown:
            return "Lockdown";
        case Reason::KeyOverride:
            return "KeyOverride";
        case Reason::Watchdog:
            return "Watchdog";
        case Reason::PermitFault:
            return "PermitFault";
        case Reason::PositionUnknown:
            return "PositionUnknown";
        case Reason::Busy:
            return "Busy";
    }
    return "?";
}

}  // namespace gate::state_machine
