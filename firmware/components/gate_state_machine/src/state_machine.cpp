// state_machine.cpp — transition table for the gate.
//
// The implementation deliberately uses a flat switch instead of a
// lookup table: there are 8 × 11 = 88 (state, event) combinations and
// most are "ignore". A switch keeps the legal transitions readable, the
// illegal ones explicit, and the binary small enough that ESP-IDF's
// LTO can fold most of it into a few jumps.

#include "gate_state_machine/state_machine.hpp"

namespace gate::state_machine {

namespace {

void push(Outputs& o, Action a) noexcept {
    if (o.count < kMaxActionsPerStep) {
        o.actions[o.count++] = a;
    }
}

}  // namespace

StateMachine::StateMachine(Config cfg) noexcept : cfg_(cfg) {}

// ---------- init helpers -----------------------------------------------------

Outputs StateMachine::init_closed() noexcept {
    Outputs o{};
    state_  = State::Closed;
    reason_ = Reason::None;
    o.new_state    = state_;
    o.transitioned = true;
    push(o, Action::StopMotor);
    push(o, Action::SetLedClosed);
    push(o, Action::EmitTelemetryStateChanged);
    return o;
}

Outputs StateMachine::init_open() noexcept {
    Outputs o{};
    state_  = State::Open;
    reason_ = Reason::None;
    o.new_state    = state_;
    o.transitioned = true;
    push(o, Action::StopMotor);
    push(o, Action::SetLedOpen);
    push(o, Action::EmitTelemetryStateChanged);
    return o;
}

Outputs StateMachine::init_unknown() noexcept {
    Outputs o{};
    state_  = State::Faulted;
    reason_ = Reason::LimitSwitchConflict;
    o.new_state    = state_;
    o.transitioned = true;
    o.reason       = reason_;
    push(o, Action::StopMotor);
    push(o, Action::SetLedFault);
    push(o, Action::EmitTelemetryStateChanged);
    return o;
}

// ---------- main transition logic --------------------------------------------

Outputs StateMachine::handle(Event ev) noexcept {
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
        o.new_state    = state_;
        o.reason       = reason_;
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
        state_  = State::Initializing;
        reason_ = Reason::None;
        o.new_state    = state_;
        o.transitioned = true;
        push(o, Action::EmitTelemetryStateChanged);
        return o;
    }

    // Motor runtime watchdog → fault.
    if (ev == Event::MotorTimeout) {
        if (state_ == State::Opening || state_ == State::Closing) {
            state_  = State::Faulted;
            reason_ = Reason::MotorTimeout;
            o.new_state    = state_;
            o.reason       = reason_;
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
                o.new_state    = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorOpen);
                push(o, Action::SetLedOpening);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case State::Opening:
            if (ev == Event::LimitOpenHit) {
                state_ = State::Open;
                o.new_state    = state_;
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
                o.new_state    = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorClose);
                push(o, Action::SetLedClosing);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case State::Closing:
            if (ev == Event::LimitClosedHit) {
                state_ = State::Closed;
                o.new_state    = state_;
                o.transitioned = true;
                push(o, Action::StopMotor);
                push(o, Action::SetLedClosed);
                push(o, Action::EmitTelemetryStateChanged);
            } else if (ev == Event::SafetyBeamTripped) {
                // Beam break during Closing is the canonical "stop and
                // reverse" scenario — the skeleton stops only. The
                // reverse-to-open behaviour is a config decision that
                // belongs in 4.5.2 where operator policy is available.
                state_  = State::StoppedClose;
                reason_ = Reason::SafetyBeamObstacle;
                o.new_state    = state_;
                o.reason       = reason_;
                o.transitioned = true;
                push(o, Action::StopMotor);
                push(o, Action::SetLedStopped);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

        case State::StoppedOpen:
            // Resume mid-open on a fresh CommandOpen.
            if (ev == Event::CommandOpen) {
                state_  = State::Opening;
                reason_ = Reason::None;
                o.new_state    = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorOpen);
                push(o, Action::SetLedOpening);
                push(o, Action::EmitTelemetryStateChanged);
            } else if (ev == Event::CommandClose && beam_clear_) {
                state_  = State::Closing;
                reason_ = Reason::None;
                o.new_state    = state_;
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
                state_  = State::Opening;
                reason_ = Reason::None;
                o.new_state    = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorOpen);
                push(o, Action::SetLedOpening);
                push(o, Action::EmitTelemetryStateChanged);
            } else if (ev == Event::CommandClose && beam_clear_) {
                state_  = State::Closing;
                reason_ = Reason::None;
                o.new_state    = state_;
                o.transitioned = true;
                push(o, Action::DriveMotorClose);
                push(o, Action::SetLedClosing);
                push(o, Action::EmitTelemetryStateChanged);
            }
            break;

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
        case State::Initializing: return "Initializing";
        case State::Closed:       return "Closed";
        case State::Opening:      return "Opening";
        case State::Open:         return "Open";
        case State::Closing:      return "Closing";
        case State::StoppedOpen:  return "StoppedOpen";
        case State::StoppedClose: return "StoppedClose";
        case State::Faulted:      return "Faulted";
    }
    return "?";
}

std::string_view to_string(Event e) noexcept {
    switch (e) {
        case Event::CommandOpen:         return "CommandOpen";
        case Event::CommandClose:        return "CommandClose";
        case Event::CommandStop:         return "CommandStop";
        case Event::LimitOpenHit:        return "LimitOpenHit";
        case Event::LimitOpenReleased:   return "LimitOpenReleased";
        case Event::LimitClosedHit:      return "LimitClosedHit";
        case Event::LimitClosedReleased: return "LimitClosedReleased";
        case Event::SafetyBeamTripped:   return "SafetyBeamTripped";
        case Event::SafetyBeamCleared:   return "SafetyBeamCleared";
        case Event::MotorTimeout:        return "MotorTimeout";
        case Event::FaultCleared:        return "FaultCleared";
    }
    return "?";
}

std::string_view to_string(Action a) noexcept {
    switch (a) {
        case Action::None:                      return "None";
        case Action::DriveMotorOpen:            return "DriveMotorOpen";
        case Action::DriveMotorClose:           return "DriveMotorClose";
        case Action::StopMotor:                 return "StopMotor";
        case Action::SetLedClosed:              return "SetLedClosed";
        case Action::SetLedOpening:             return "SetLedOpening";
        case Action::SetLedOpen:                return "SetLedOpen";
        case Action::SetLedClosing:             return "SetLedClosing";
        case Action::SetLedStopped:             return "SetLedStopped";
        case Action::SetLedFault:               return "SetLedFault";
        case Action::EmitTelemetryStateChanged: return "EmitTelemetryStateChanged";
    }
    return "?";
}

std::string_view to_string(Reason r) noexcept {
    switch (r) {
        case Reason::None:                return "None";
        case Reason::OperatorStop:        return "OperatorStop";
        case Reason::SafetyBeamObstacle:  return "SafetyBeamObstacle";
        case Reason::MotorTimeout:        return "MotorTimeout";
        case Reason::LimitSwitchConflict: return "LimitSwitchConflict";
        case Reason::UnexpectedEvent:     return "UnexpectedEvent";
    }
    return "?";
}

}  // namespace gate::state_machine
