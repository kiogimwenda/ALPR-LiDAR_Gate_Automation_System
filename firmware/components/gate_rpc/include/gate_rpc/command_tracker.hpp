// command_tracker.hpp — pending-GateCommand completion tracking (pure C++20).
//
// gRPC command flow (proto contract): the firmware acks every
// GateCommand twice — immediately on receipt (completed=false) and
// again when execution finishes (completed=true, success/error,
// state_after). For asynchronous motion commands "finishes" means the
// gate reached a terminal state, faulted, stopped short mid-travel,
// was refused by the safety interlock, or ran out the clock. Those
// five rules are pure logic on (state, reason, transitioned, time) —
// so they live here, with no FreeRTOS/nghttp2/nanopb anywhere in
// sight, host-tested like gate_state_machine and the framing codec.
// ControlClient owns one tracker under a mutex and turns its verdicts
// into CommandAck envelopes.
//
// One command in flight at a time, matching the physical device: a
// gate cannot execute two motion commands concurrently, and the
// server has no business pipelining them. Arming over a live command
// supersedes it (the tracker hands back a failure verdict for the old
// one so the server is never left waiting).

#pragma once

#include <cstdint>

#include "gate_state_machine/state_machine.hpp"

namespace gate::rpc {

class CommandTracker {
public:
    static constexpr std::size_t kMaxCommandId = 40;  // UUIDv4 + NUL (proto options)

    // A completion decision. `fire` is true at most once per armed
    // command. `command_id` is a copy, not a pointer into the tracker:
    // arm() both returns a verdict (for the superseded command) and
    // overwrites the id buffer (with the new command), so an aliasing
    // pointer would report the wrong id.
    struct Verdict {
        bool fire = false;
        bool success = false;
        char command_id[kMaxCommandId] = {};
        const char* error = "";  // set when !success; always a literal
    };

    // Begin tracking. `terminal_ok` is the state that completes the
    // command successfully; `deadline_ms` is absolute (caller clock).
    // Returns a superseded-failure Verdict for any command still in
    // flight, which the caller must ack before the new one runs.
    Verdict arm(const char* command_id, gate::state_machine::State terminal_ok,
                std::uint32_t deadline_ms) noexcept;

    // Feed a gate event (mirrors GateController's TransitionListener).
    Verdict on_gate_event(gate::state_machine::State s, gate::state_machine::Reason r,
                          bool transitioned) noexcept;

    // Feed the clock (call periodically; absolute ms, same base as arm).
    Verdict on_tick(std::uint32_t now_ms) noexcept;

    [[nodiscard]] bool armed() const noexcept { return armed_; }

private:
    Verdict resolve(bool success, const char* error) noexcept;

    char id_[kMaxCommandId] = {};
    gate::state_machine::State terminal_ok_{};
    std::uint32_t deadline_ms_ = 0;
    bool armed_ = false;
};

}  // namespace gate::rpc
