// command_tracker.cpp — the five completion rules.

#include "gate_rpc/command_tracker.hpp"

#include <cstring>

namespace gate::rpc {

namespace sm = gate::state_machine;

CommandTracker::Verdict CommandTracker::arm(const char* command_id, sm::State terminal_ok,
                                            std::uint32_t deadline_ms) noexcept {
    Verdict superseded{};
    if (armed_) {
        superseded = resolve(false, "superseded by a newer command");
    }
    const std::size_t n = strnlen(command_id, sizeof(id_) - 1);
    std::memcpy(id_, command_id, n);
    id_[n] = '\0';
    terminal_ok_ = terminal_ok;
    deadline_ms_ = deadline_ms;
    armed_ = true;
    return superseded;
}

CommandTracker::Verdict CommandTracker::on_gate_event(sm::State s, sm::Reason r,
                                                      bool transitioned) noexcept {
    if (!armed_) {
        return {};
    }
    // Rule 1 — success: the commanded terminal state was reached.
    if (transitioned && s == terminal_ok_) {
        return resolve(true, "");
    }
    // Rule 2 — hard failure: the gate faulted while the command ran.
    if (transitioned && s == sm::State::Faulted) {
        return resolve(false, sm::to_string(r).data());
    }
    // Rule 3 — aborted mid-travel: the commanded motion stopped short
    // (operator stop, or the safety beam tripping during a close).
    // Definitive — the server hears the abort and its reason now, not
    // at the deadline. Found while writing the Phase 4.5.6 scenario
    // tests: a beam-aborted close previously sat silent for the full
    // deadline window.
    if (transitioned && (s == sm::State::StoppedOpen || s == sm::State::StoppedClose)) {
        return resolve(false, sm::to_string(r).data());
    }
    // Rule 4 — interlock refusal: no transition, but the step carried
    // a Reason (a close rejected outright by the safety-beam latch).
    if (!transitioned && r == sm::Reason::SafetyBeamObstacle) {
        return resolve(false, "rejected: safety beam obstacle");
    }
    // Intermediate transitions (Opening, Closing, …) leave the command
    // pending — the deadline is the backstop.
    return {};
}

CommandTracker::Verdict CommandTracker::on_tick(std::uint32_t now_ms) noexcept {
    if (!armed_ || static_cast<std::int32_t>(now_ms - deadline_ms_) < 0) {
        return {};
    }
    // Rule 5 — deadline: no terminal state before the clock ran out.
    return resolve(false, "deadline exceeded awaiting terminal state");
}

CommandTracker::Verdict CommandTracker::resolve(bool success, const char* error) noexcept {
    armed_ = false;
    Verdict v;
    v.fire = true;
    v.success = success;
    std::memcpy(v.command_id, id_, sizeof(id_));
    v.error = error;
    return v;
}

}  // namespace gate::rpc
