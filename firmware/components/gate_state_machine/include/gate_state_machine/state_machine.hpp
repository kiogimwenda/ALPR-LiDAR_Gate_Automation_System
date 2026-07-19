// state_machine.hpp — pure C++20 gate state machine.
//
// This module is hardware-agnostic on purpose. It knows nothing about
// ESP-IDF, FreeRTOS, GPIO, or RPC. It takes Events (edge-triggered
// observations of the world) and returns Outputs (a transition + a small
// list of Actions for the driver layer to execute). That separation lets
// the same logic run in three places:
//
//   - On-target (Phase 4.5.2 wires it to gate_drivers).
//   - In host unit tests (tests/state_machine/, Catch2).
//   - In simulation (Phase 4.7 driver-replay harness).
//
// The state set covers the residential sliding gate that Phase 1 picked
// as the first deployment target. Dual-leaf gates reduce to the same
// states with timing offsets handled in the driver layer, so the
// skeleton is type-agnostic — Config::gate_type is recorded for
// telemetry only.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gate::state_machine {

// Gate position / motion. Order is stable so it can be serialised to
// telemetry as an integer without re-mapping.
enum class State : std::uint8_t {
    Initializing = 0,  // boot — determine position from limit switches
    Closed = 1,        // close limit active, motor off
    Opening = 2,       // motor energised open, neither limit active
    Open = 3,          // open limit active, motor off
    Closing = 4,       // motor energised close, neither limit active
    StoppedOpen = 5,   // paused mid-travel during an Open command
    StoppedClose = 6,  // paused mid-travel during a Close command
    Faulted = 7,       // unrecoverable without operator reset
};

// Edge-triggered inputs from the outside world. The driver layer
// debounces / de-glitches and only emits events on confirmed changes.
enum class Event : std::uint8_t {
    CommandOpen = 0,          // RPC, radio button, or schedule
    CommandClose = 1,         // RPC, radio, or auto-close timer expiry
    CommandStop = 2,          // operator emergency stop
    LimitOpenHit = 3,         // open-position limit switch closed
    LimitOpenReleased = 4,    // open limit released (motion began)
    LimitClosedHit = 5,       // close-position limit switch closed
    LimitClosedReleased = 6,  // close limit released (motion began)
    SafetyBeamTripped = 7,    // IR beam broken (obstruction)
    SafetyBeamCleared = 8,    // beam restored
    MotorTimeout = 9,         // travel exceeded configured runtime budget
    FaultCleared = 10,        // operator reset from Faulted
};

// Side-effects the state machine asks the driver layer to perform on a
// transition. Drivers execute these in order; an Action never carries
// arbitrary data so the list stays trivially copyable.
//
// Timers (motor-runtime watchdog, auto-close countdown) are owned by
// the driver layer — it starts them when entering Opening/Closing/Open
// and cancels them on the inverse transitions. Keeping timers out of
// the Action enum stops the state machine from caring about wall-clock
// time and keeps host unit tests deterministic.
enum class Action : std::uint8_t {
    None = 0,
    DriveMotorOpen = 1,
    DriveMotorClose = 2,
    StopMotor = 3,
    SetLedClosed = 4,
    SetLedOpening = 5,
    SetLedOpen = 6,
    SetLedClosing = 7,
    SetLedStopped = 8,
    SetLedFault = 9,
    EmitTelemetryStateChanged = 10,
};

// Why a Stopped or Faulted state was entered. Surfaces in telemetry and
// in the dashboard so an operator can see "stopped: safety beam" vs
// "stopped: operator". Kept terse so the union with State stays cheap.
enum class Reason : std::uint8_t {
    None = 0,
    OperatorStop = 1,
    SafetyBeamObstacle = 2,
    MotorTimeout = 3,
    LimitSwitchConflict = 4,  // both limits active simultaneously — wiring fault
    UnexpectedEvent = 5,      // event arrived in a state that should not see it
};

// Worst-case actions emitted on a single transition is 3 (drive/stop
// motor + set LED + emit telemetry). The fixed-size buffer keeps
// Outputs trivially copyable and stack-allocatable in the FreeRTOS
// task that will pump events in Phase 4.5.2.
constexpr std::size_t kMaxActionsPerStep = 3;

struct Outputs {
    std::array<Action, kMaxActionsPerStep> actions{};
    std::uint8_t count = 0;  // valid entries in `actions`
    State new_state = State::Initializing;
    bool transitioned = false;
    Reason reason = Reason::None;
};

// Gate geometry / behaviour tuning. The state machine itself doesn't
// time anything (the driver layer does — it owns hardware timers); the
// Config is carried for telemetry and for sanity-checking that the
// caller has wired the right timers.
enum class GateType : std::uint8_t {
    Sliding = 0,
    DualLeaf = 1,
};

struct Config {
    GateType gate_type = GateType::Sliding;
    std::uint32_t motor_timeout_ms = 30'000;  // 30 s default for a 4 m gate
    std::uint32_t auto_close_ms = 0;          // 0 disables auto-close
};

// Human-readable names for logging / telemetry / tests. Returning
// string_view keeps the call cheap and avoids dragging in <string>.
std::string_view to_string(State) noexcept;
std::string_view to_string(Event) noexcept;
std::string_view to_string(Action) noexcept;
std::string_view to_string(Reason) noexcept;

class StateMachine {
public:
    explicit StateMachine(Config cfg = {}) noexcept;

    // Boot-time position resolution. The driver layer reads both limit
    // switches once before pumping events, then calls one of these to
    // tell the state machine where the gate actually is. Returns the
    // emitted Outputs (LED + telemetry).
    Outputs init_closed() noexcept;
    Outputs init_open() noexcept;
    Outputs init_unknown() noexcept;  // neither limit asserted → Faulted

    // Drive the state machine. Pure function of (state, reason, event).
    // Does not block; never allocates.
    Outputs handle(Event ev) noexcept;

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] Reason reason() const noexcept { return reason_; }
    [[nodiscard]] const Config& config() const noexcept { return cfg_; }

private:
    Config cfg_{};
    State state_ = State::Initializing;
    Reason reason_ = Reason::None;
    // Safety-beam latch — the gate must not close while a beam break is
    // active. The state machine remembers the live beam status so a
    // CommandClose that arrives mid-obstacle is rejected.
    bool beam_clear_ = true;
};

}  // namespace gate::state_machine
