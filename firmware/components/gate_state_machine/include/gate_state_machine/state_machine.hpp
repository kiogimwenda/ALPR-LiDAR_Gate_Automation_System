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
// Two modes share this class. With Config::lock_layout == None (rev 2
// boards) the machine behaves exactly as deployed: the original eight
// states and transitions. Any other layout selects the lock-sequenced
// table of docs/design/gate-system-v2.md section 8, in which the
// machine supervises locks in front of a commercial operator. Limit
// events are aggregated by the driver layer: LimitOpenHit and
// LimitClosedHit mean every leaf, so gate types differ only in driver
// timing and Config::gate_type stays informational here.

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
    // Lock-sequenced states. Unreachable with LockLayout::None.
    Unlocking = 8,        // locks releasing; motor off
    Locking = 9,          // at closed limit; locks engaging
    ClosedInsecure = 10,  // shut, at least one lock unproven
    PreClose = 11,        // bolts retracting before a close
    Verifying = 12,       // gate-type pulse
    Calibrating = 13,     // measured open/close cycles
    Lockdown = 14,        // closed, locked, remote opens refused
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
    // Lock-sequenced events; dropped with LockLayout::None.
    LocksReleased = 11,
    LocksEngaged = 12,
    LockFailed = 13,
    LockDropped = 14,
    OperatorMoving = 15,
    OperatorNoResponse = 16,
    OperatorDirectionWrong = 17,
    OperatorCollision = 18,
    OperatorReversed = 19,
    KeyOverrideOn = 20,
    KeyOverrideOff = 21,
    LocalReset = 22,  // RTE held 5 s
    CurrentOverrun = 23,
    BeginVerify = 24,
    VerifyPassed = 25,
    VerifyFailed = 26,
    BeginCalibrate = 27,
    CalibrateDone = 28,
    CalibrateFailed = 29,
    LockdownOn = 30,
    LockdownOff = 31,
    GateForced = 32,
    BatteryCritical = 33,
    // Additions to the spec's list, needed to express its section 7 rules.
    BoltsRested = 34,        // bolt_hold_open_s expired; bolts dropped, permit off
    LockFeedbackFault = 35,  // LockFailed with a contradictory feedback input
    PermitFault = 36,        // K_PERMIT readback disagrees
};

// Who asked for an open. Only the exit button carries the local
// authority: it beats the lockdown latch and the battery reserve.
enum class OpenOrigin : std::uint8_t { Remote = 0, Rte = 1 };

// Battery band, from the driver layer's thresholds (spec section 12.3).
enum class BatteryLevel : std::uint8_t {
    Ok = 0,            // at or above reserve_v
    BelowReserve = 1,  // RTE only
    BelowCutoff = 2,   // nothing moves; mechanical release
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
    ReleaseLocks = 11,  // full unlock sequence, section 7.1
    EngageLocks = 12,   // full lock sequence, section 7.2
    RetractBolts = 13,  // ensure bolts are retracted; confirms at once if they are
    SetPermit = 14,
    ClearPermit = 15,
    RunVerifyPulse = 16,
    RunCalibration = 17,
    RaiseAlert = 18,       // severity follows Outputs::reason
    PersistLockdown = 19,  // write lockdown_latched() to NVS
    RestBolts = 20,        // cancel any sequence, bolts off (spring extended)
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
    LockJam = 6,
    LockIncomplete = 7,
    LockFeedback = 8,
    OperatorFault = 9,
    OperatorCollision = 10,
    GateTypeMismatch = 11,
    CalibrationFailed = 12,
    BatteryReserve = 13,
    ForcedEntry = 14,
    CurrentAnomaly = 15,
    Lockdown = 16,
    KeyOverride = 17,
    Watchdog = 18,
    PermitFault = 19,
    PositionUnknown = 20,  // mid-travel at boot, lock-sequenced mode
    Busy = 21,             // refused: verify or calibration in progress
};

// A step can chain two transitions (lockdown during a mid-travel
// Unlocking stops, then starts the close). The state-by-event test
// proves no step fills this buffer; push() asserts it. The fixed-size buffer keeps
// Outputs trivially copyable and stack-allocatable in the FreeRTOS
// task that pumps events.
constexpr std::size_t kMaxActionsPerStep = 8;

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
    SingleLeaf = 2,
};

// None selects the legacy rev 2 behaviour; see the file header.
enum class LockLayout : std::uint8_t {
    None = 0,
    Four = 1,  // sliding, single leaf: 2 bolts + 2 maglocks
    Five = 2,  // dual leaf: 2 bolts + 3 maglocks
};

// Who owns reversal on a beam trip. A commercial operator reverses by
// itself, so the firmware follows and must not send a second command.
enum class ActuatorProfile : std::uint8_t { Contactor = 0, Operator = 1 };

struct Config {
    GateType gate_type = GateType::Sliding;
    std::uint32_t motor_timeout_ms = 30'000;  // 30 s default for a 4 m gate
    std::uint32_t auto_close_ms = 0;          // 0 disables auto-close
    LockLayout lock_layout = LockLayout::None;
    ActuatorProfile profile = ActuatorProfile::Contactor;
};

// Human-readable names for logging / telemetry / tests. Returning
// string_view keeps the call cheap and avoids dragging in <string>.
// GateState wire number per spec section 8.5. Numeric so this module
// stays free of generated code; firmware and simulator cast it.
[[nodiscard]] constexpr std::uint8_t wire_state(State s) noexcept {
    switch (s) {
        case State::Initializing:
            return 0;
        case State::Closed:
            return 1;
        case State::Opening:
            return 2;
        case State::Open:
            return 3;
        case State::Closing:
            return 4;
        case State::Faulted:
            return 5;
        case State::Lockdown:
            return 6;
        case State::Unlocking:
            return 7;
        case State::Locking:
            return 8;
        case State::ClosedInsecure:
            return 9;
        case State::Verifying:
            return 10;
        case State::Calibrating:
            return 11;
        case State::PreClose:
            return 12;
        case State::StoppedOpen:
        case State::StoppedClose:
            return 13;
    }
    return 0;
}

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
    // locks_proven only matters in lock-sequenced mode: unproven locks
    // boot to ClosedInsecure, and a set lockdown latch boots to Lockdown.
    Outputs init_closed(bool locks_proven = true) noexcept;
    Outputs init_open() noexcept;
    Outputs init_unknown() noexcept;         // neither limit asserted → Faulted
    Outputs init_limit_conflict() noexcept;  // both limits asserted → Faulted

    // Lockdown is a latch persisted by the driver layer. Call before
    // init_*() on boot. Ignored with LockLayout::None.
    void restore_lockdown(bool latched) noexcept;
    void set_battery(BatteryLevel level) noexcept { battery_ = level; }

    // Drive the state machine. Pure function of (state, reason, event).
    // Does not block; never allocates.
    // `origin` only matters for CommandOpen.
    Outputs handle(Event ev, OpenOrigin origin = OpenOrigin::Remote) noexcept;

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] Reason reason() const noexcept { return reason_; }
    [[nodiscard]] const Config& config() const noexcept { return cfg_; }
    [[nodiscard]] bool lockdown_latched() const noexcept { return lockdown_; }
    [[nodiscard]] bool locks_free() const noexcept { return locks_free_; }

private:
    Config cfg_{};
    State state_ = State::Initializing;
    Reason reason_ = Reason::None;
    // Safety-beam latch — the gate must not close while a beam break is
    // active. The state machine remembers the live beam status so a
    // CommandClose that arrives mid-obstacle is rejected.
    bool beam_clear_ = true;

    // Lock-sequenced mode (state_machine_locked.cpp).
    [[nodiscard]] bool locked_mode() const noexcept { return cfg_.lock_layout != LockLayout::None; }
    Outputs handle_locked(Event ev, OpenOrigin origin) noexcept;
    void enter(Outputs& o, State s, Reason r) noexcept;
    void enter_locking(Outputs& o, Reason r) noexcept;
    void enter_fault(Outputs& o, Reason r, bool stop_motor) noexcept;
    void begin_close(Outputs& o) noexcept;
    void try_lockdown_close(Outputs& o) noexcept;
    void finish_procedure(Outputs& o, Reason r, bool alert) noexcept;
    void abort_unlock(Outputs& o, Reason r) noexcept;
    void on_open(Outputs& o, OpenOrigin origin) noexcept;
    void on_close(Outputs& o) noexcept;
    void on_stop(Outputs& o) noexcept;
    void on_key_on(Outputs& o) noexcept;
    void on_lockdown_on(Outputs& o) noexcept;
    void on_lock_failure(Outputs& o, Reason r) noexcept;
    void on_insecure(Outputs& o, Reason r) noexcept;
    void follow_hand_move(Outputs& o, Reason r) noexcept;
    void on_state_event(Outputs& o, Event ev) noexcept;

    BatteryLevel battery_ = BatteryLevel::Ok;
    State pre_close_from_ = State::Open;  // where a cancelled PreClose returns
    bool lockdown_ = false;               // persisted latch, independent of state
    bool close_pending_ = false;          // lockdown close waiting on beam or Open
    bool rte_cycle_ = false;              // current open came from the exit button
    bool key_active_ = false;
    bool locks_free_ = false;  // bolts proven retracted, maglocks released
    bool at_closed_ = false;   // closed limit asserted
};

}  // namespace gate::state_machine
