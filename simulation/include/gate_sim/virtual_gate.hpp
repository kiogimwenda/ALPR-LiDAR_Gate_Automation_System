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

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "gate_rpc/command_tracker.hpp"
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
    };

    struct Ack {
        std::string command_id;
        bool success = false;
        std::string error;
    };

    enum class Command { Open, Close };

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

    // Advance simulated time. Call at a steady cadence (e.g. 50 ms).
    void advance(std::uint32_t dt_ms);

    // Snapshots for telemetry.
    [[nodiscard]] state_machine::State state() const;
    [[nodiscard]] bool limit_open() const;
    [[nodiscard]] bool limit_closed() const;
    [[nodiscard]] bool beam_clear() const;

    // Completed-command verdicts accumulated since the last drain.
    std::vector<Ack> drain_acks();

    // One-line human summary for the interactive console.
    [[nodiscard]] std::string status_line() const;

private:
    void step_locked(state_machine::Event ev);
    void apply_locked(const state_machine::Outputs& out);
    void resolve_position_locked();
    void record(const gate::rpc::CommandTracker::Verdict& v);

    Config cfg_;
    mutable std::mutex mutex_;

    state_machine::StateMachine machine_;
    gate::rpc::CommandTracker tracker_;

    std::uint64_t now_ms_ = 0;
    double position_ms_ = 0.0;  // 0 = closed limit … travel_ms = open limit
    bool motor_open_ = false;
    bool motor_close_ = false;
    bool beam_blocked_ = false;
    std::uint64_t watchdog_deadline_ = 0;   // 0 = disarmed
    std::uint64_t autoclose_deadline_ = 0;  // 0 = disarmed

    std::vector<Ack> acks_;
};

}  // namespace gate::sim
