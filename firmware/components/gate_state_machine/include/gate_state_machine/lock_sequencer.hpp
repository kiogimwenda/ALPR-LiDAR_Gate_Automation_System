// lock_sequencer.hpp — pure timing of the unlock and lock sequences
// (docs/design/gate-system-v2.md section 7), shared by firmware and
// simulator. It takes time and feedback and returns drive levels and a
// result; the caller turns the result into a state machine event:
// Released → LocksReleased, Engaged → LocksEngaged, Failed → LockFailed
// (LockFeedbackFault when fault == Feedback).

#pragma once

#include <array>
#include <cstdint>

#include "gate_state_machine/state_machine.hpp"

namespace gate::state_machine {

struct LockTimingConfig {
    std::uint32_t mag_release_ms = 500;
    std::uint32_t bolt_retract_ms = 800;
    std::uint32_t bolt_rest_ms = 500;  // coil off between pulls
    std::uint32_t pre_move_ms = 200;
    std::uint32_t settle_ms = 300;
    std::uint32_t mag_bond_ms = 2'000;
    std::uint32_t bolt_extend_ms = 1'000;
    std::uint32_t repull_ms = 500;
    std::uint32_t feedback_fault_ms = 300;
    std::uint32_t retries = 2;
};

struct LockFeedback {
    std::array<bool, 2> bolt_retracted{};
    std::array<bool, 2> bolt_extended{};
    std::array<bool, 3> mag_bonded{};  // M3 ignored on the 4-lock layout
};

enum class LockFault : std::uint8_t { None, MaglockStuck, BoltJam, Feedback, Incomplete };

class LockSequencer {
public:
    enum class Result : std::uint8_t { None, Released, Engaged, Failed };

    LockSequencer(LockLayout layout, LockTimingConfig timing) noexcept;

    void start_release(std::uint64_t now_ms) noexcept;  // section 7.1
    void start_retract(std::uint64_t now_ms) noexcept;  // bolts only, before a close
    void start_engage(std::uint64_t now_ms) noexcept;   // section 7.2
    void abort() noexcept;
    // Bolts dropped by the hold timer; maglocks stay off until a close.
    void rest_bolts() noexcept { bolt_on_ = false; }
    // Boot on a proven closed gate: maglocks held, bolts at rest.
    void assume_engaged() noexcept;

    Result tick(std::uint64_t now_ms, const LockFeedback& fb) noexcept;

    [[nodiscard]] bool bolt_drive() const noexcept { return bolt_on_; }
    [[nodiscard]] bool maglock_drive() const noexcept { return mag_on_; }
    [[nodiscard]] bool busy() const noexcept { return phase_ != Phase::Idle; }
    [[nodiscard]] LockFault fault() const noexcept { return fault_; }

private:
    enum class Phase : std::uint8_t {
        Idle,
        MagRelease,
        BoltPull,
        BoltRest,
        PreMove,
        Settle,
        MagBond,
        BoltDrop,
        BoltRepull,
    };

    void go(Phase p, std::uint64_t now_ms, std::uint32_t timeout_ms) noexcept;
    Result fail(LockFault f, bool restore) noexcept;
    Result finish_engage() noexcept;
    [[nodiscard]] bool feedback_contradictory(std::uint64_t now_ms,
                                              const LockFeedback& fb) noexcept;
    [[nodiscard]] bool any_bonded(const LockFeedback& fb) const noexcept;
    [[nodiscard]] bool all_bonded(const LockFeedback& fb) const noexcept;

    LockTimingConfig t_;
    std::size_t mags_;
    Phase phase_ = Phase::Idle;
    LockFault fault_ = LockFault::None;
    std::uint64_t deadline_ = 0;
    std::array<std::uint64_t, 2> bad_since_{};  // 0 = bolt feedback consistent
    std::uint32_t tries_ = 0;
    bool mag_retried_ = false;
    bool mag_failed_ = false;
    bool bolt_on_ = false;
    bool mag_on_ = false;
};

}  // namespace gate::state_machine
