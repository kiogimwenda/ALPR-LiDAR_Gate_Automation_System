// virtual_gate.hpp — a virtual gate: the firmware's pure core with
// simulated physics, on the host, under wall-clock time.
//
// This is the Phase 4.5.6 SimulatedGate test harness grown into a
// product: the same gate_state_machine and CommandTracker the ESP32-S3
// runs, composed with the same GateController wiring (action
// execution, watchdog/auto-close timers, reverse-on-beam and
// auto-close-retry policies, the transition→tracker listener) — but
// the motor moves a simulated position instead of a contactor, and
// advance(dt) is driven by the caller's clock instead of FreeRTOS.
//
// Position model: 0 ms = on the closed limit, travel_ms = on the open
// limit. Motion integrates dt, so a resume after a mid-travel stop
// takes exactly the remaining distance — which makes watchdog and
// auto-close interactions behave like the real machine, not like a
// state chart.
//
// Thread-safety: every public method takes the internal mutex. The
// SimClient's session loop and an interactive stdin thread can both
// poke the gate concurrently, same as RPC vs operator on the real
// device.

#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "gate_rpc/command_tracker.hpp"
#include "gate_state_machine/lock_sequencer.hpp"
#include "gate_state_machine/state_machine.hpp"

namespace gate::sim {

class VirtualGate {
public:
    struct Config {
        std::uint32_t travel_ms = 12'000;  // full stroke, closed ↔ open
        std::uint32_t motor_timeout_ms = 30'000;
        std::uint32_t auto_close_ms = 0;  // 0 disables
        bool reverse_on_beam = true;
        std::uint32_t command_deadline_ms = 40'000;

        // Gate model. The defaults are the legacy rev 2 contactor gate.
        state_machine::GateType gate_type = state_machine::GateType::Sliding;
        state_machine::LockLayout lock_layout = state_machine::LockLayout::None;
        state_machine::ActuatorProfile profile = state_machine::ActuatorProfile::Contactor;
        std::uint32_t leaf_delay_ms = 2'000;      // dual leaf: leaf B lags on open, A on close
        std::uint32_t offline_close_ms = 30'000;  // close after an RTE under lockdown
        std::uint32_t bolt_hold_open_ms = 45'000;
        std::uint32_t verify_pulse_ms = 2'500;
        std::uint32_t bolt_transit_ms = 150;  // both feedback contacts open in transit
        std::uint32_t mag_transit_ms = 100;
        state_machine::LockTimingConfig lock_timing{};
    };

    struct Ack {
        std::string command_id;
        bool success = false;
        std::string error;
    };

    enum class Command : std::uint8_t { kOpen, kClose };

    explicit VirtualGate(const Config& cfg);

    // Boot with the gate parked on its closed limit.
    void boot_closed();

    // Server-tracked motion command: arms the tracker (superseding any
    // in-flight command) and issues the event. Completion surfaces via
    // drain_acks() once the physics get there.
    void issue(const std::string& command_id, Command c);

    // Local / operator actions — no tracker involvement, mirroring a
    // radio button or a hand on the physical device.
    void local_open();
    void local_close();
    void operator_stop();
    void clear_fault();  // re-resolves position, like the driver layer
    void reboot();       // re-init from the current physical position
    void trip_beam();
    void clear_beam();

    // Exit button: the local authority path.
    void rte();
    // Latches. latch_open() holds the gate open with no auto-close;
    // latch_close() is the persisted lockdown latch. release_latch()
    // clears both. Each returns false when refused or unsupported.
    bool latch_open();
    bool latch_close();
    void release_latch();

    // Faults and hardware the firmware cannot command.
    void jam_bolt(std::size_t index, bool jammed);
    void stick_maglock(std::size_t index, bool stuck);
    void set_key_override(bool active);
    void set_battery(state_machine::BatteryLevel level);
    void begin_verify(state_machine::GateType actual);
    // Injected firmware bug: drive the motor with no state machine
    // involvement, to prove the independent interlock holds.
    void force_motor_open();

    // Advance simulated time. Call at a steady cadence (e.g. 50 ms).
    void advance(std::uint32_t dt_ms);

    // Snapshots for telemetry.
    [[nodiscard]] state_machine::State state() const;
    [[nodiscard]] bool limit_open() const;
    [[nodiscard]] bool limit_closed() const;
    [[nodiscard]] bool beam_clear() const;
    [[nodiscard]] double position_permille_a() const;
    [[nodiscard]] double position_permille_b() const;
    [[nodiscard]] bool bolts_extended() const;
    [[nodiscard]] bool maglocks_bonded() const;
    [[nodiscard]] bool permit() const;
    [[nodiscard]] bool lockdown_latched() const;
    [[nodiscard]] state_machine::Reason reason() const;

    // Completed-command verdicts accumulated since the last drain.
    std::vector<Ack> drain_acks();

    // One-line human summary for the interactive console.
    [[nodiscard]] std::string status_line() const;

private:
    void step_locked(state_machine::Event ev);
    void apply_locked(const state_machine::Outputs& out);
    void resolve_position_locked();
    void record(const gate::rpc::CommandTracker::Verdict& v);
    [[nodiscard]] state_machine::Config machine_config() const;
    [[nodiscard]] bool locked_gate() const;
    [[nodiscard]] bool interlock_ok() const;
    [[nodiscard]] bool dual() const;
    [[nodiscard]] bool at_open() const;
    [[nodiscard]] bool at_closed() const;
    void move_leaves(double dt_ms, int direction);
    void advance_locks(std::uint32_t dt_ms);
    [[nodiscard]] state_machine::LockFeedback feedback() const;

    Config cfg_;
    mutable std::mutex mutex_;

    state_machine::StateMachine machine_;
    gate::rpc::CommandTracker tracker_;

    std::uint64_t now_ms_ = 0;
    state_machine::LockSequencer sequencer_;
    double position_ms_ = 0.0;         // leaf A: 0 = closed limit … travel_ms = open limit
    double position_b_ms_ = 0.0;       // leaf B, dual leaf only
    double run_ms_ = 0.0;              // time since this run began, for the leaf delay
    std::array<double, 2> bolt_ms_{};  // 0 = extended … bolt_transit_ms = retracted
    std::array<bool, 2> bolt_jam_{};
    std::array<bool, 3> mag_bonded_{};
    std::array<bool, 3> mag_stuck_{};
    std::array<double, 3> mag_ms_{};  // time since the bond last changed drive
    bool permit_ = false;
    bool key_active_ = false;
    bool open_latched_ = false;
    bool forced_motor_ = false;
    state_machine::GateType actual_type_ = state_machine::GateType::Sliding;
    std::uint64_t verify_deadline_ = 0;
    std::uint64_t calibrate_deadline_ = 0;
    std::uint64_t bolt_rest_deadline_ = 0;
    bool motor_open_ = false;
    bool motor_close_ = false;
    bool beam_blocked_ = false;
    std::uint64_t watchdog_deadline_ = 0;   // 0 = disarmed
    std::uint64_t autoclose_deadline_ = 0;  // 0 = disarmed

    std::vector<Ack> acks_;
};

}  // namespace gate::sim
