// lock_sequencer.cpp — see the header.

#include "gate_state_machine/lock_sequencer.hpp"

namespace gate::state_machine {

namespace {

bool all_retracted(const LockFeedback& fb) noexcept {
    return fb.bolt_retracted[0] && fb.bolt_retracted[1] && !fb.bolt_extended[0] &&
           !fb.bolt_extended[1];
}

bool all_extended(const LockFeedback& fb) noexcept {
    return fb.bolt_extended[0] && fb.bolt_extended[1] && !fb.bolt_retracted[0] &&
           !fb.bolt_retracted[1];
}

}  // namespace

LockSequencer::LockSequencer(LockLayout layout, LockTimingConfig timing) noexcept
    : t_(timing), mags_(layout == LockLayout::Five ? 3U : 2U) {}

void LockSequencer::go(Phase p, std::uint64_t now_ms, std::uint32_t timeout_ms) noexcept {
    phase_ = p;
    deadline_ = now_ms + timeout_ms;
}

void LockSequencer::start_release(std::uint64_t now_ms) noexcept {
    fault_ = LockFault::None;
    mag_retried_ = false;
    tries_ = 0;
    mag_on_ = false;
    go(Phase::MagRelease, now_ms, t_.mag_release_ms);
}

void LockSequencer::start_retract(std::uint64_t now_ms) noexcept {
    fault_ = LockFault::None;
    tries_ = 0;
    bolt_on_ = true;
    go(Phase::BoltPull, now_ms, t_.bolt_retract_ms);
}

void LockSequencer::start_engage(std::uint64_t now_ms) noexcept {
    fault_ = LockFault::None;
    mag_failed_ = false;
    tries_ = 0;
    go(Phase::Settle, now_ms, t_.settle_ms);
}

void LockSequencer::abort() noexcept {
    phase_ = Phase::Idle;
}

void LockSequencer::assume_engaged() noexcept {
    phase_ = Phase::Idle;
    mag_on_ = true;
    bolt_on_ = false;
}

LockSequencer::Result LockSequencer::fail(LockFault f, bool restore) noexcept {
    fault_ = f;
    phase_ = Phase::Idle;
    if (restore) {
        bolt_on_ = false;
        mag_on_ = true;
    }
    return Result::Failed;
}

LockSequencer::Result LockSequencer::finish_engage() noexcept {
    phase_ = Phase::Idle;
    if (mag_failed_) {
        fault_ = LockFault::Incomplete;
        return Result::Failed;
    }
    return Result::Engaged;
}

bool LockSequencer::feedback_contradictory(std::uint64_t now_ms, const LockFeedback& fb) noexcept {
    bool tripped = false;
    for (std::size_t i = 0; i < bad_since_.size(); ++i) {
        if (fb.bolt_retracted[i] != fb.bolt_extended[i]) {
            bad_since_[i] = 0;
        } else if (bad_since_[i] == 0) {
            bad_since_[i] = now_ms + 1;  // +1 keeps a t=0 start distinct from "clear"
        } else if (now_ms + 1 - bad_since_[i] > t_.feedback_fault_ms) {
            tripped = true;
        }
    }
    return tripped;
}

bool LockSequencer::any_bonded(const LockFeedback& fb) const noexcept {
    for (std::size_t i = 0; i < mags_; ++i) {
        if (fb.mag_bonded[i]) {
            return true;
        }
    }
    return false;
}

bool LockSequencer::all_bonded(const LockFeedback& fb) const noexcept {
    for (std::size_t i = 0; i < mags_; ++i) {
        if (!fb.mag_bonded[i]) {
            return false;
        }
    }
    return true;
}

LockSequencer::Result LockSequencer::tick(std::uint64_t now_ms, const LockFeedback& fb) noexcept {
    if (phase_ == Phase::Idle) {
        return Result::None;
    }
    const bool engaging = phase_ == Phase::Settle || phase_ == Phase::MagBond ||
                          phase_ == Phase::BoltDrop || phase_ == Phase::BoltRepull;
    if (feedback_contradictory(now_ms, fb)) {
        return fail(LockFault::Feedback, !engaging);
    }
    const bool expired = now_ms >= deadline_;

    switch (phase_) {
        case Phase::Idle:
            break;

        case Phase::MagRelease:
            if (!any_bonded(fb)) {
                bolt_on_ = true;
                go(Phase::BoltPull, now_ms, t_.bolt_retract_ms);
            } else if (expired) {
                if (mag_retried_) {
                    return fail(LockFault::MaglockStuck, true);
                }
                mag_retried_ = true;
                go(Phase::MagRelease, now_ms, t_.mag_release_ms);
            }
            break;

        case Phase::BoltPull:
            if (all_retracted(fb)) {
                go(Phase::PreMove, now_ms, t_.pre_move_ms);
            } else if (expired) {
                bolt_on_ = false;
                if (tries_ >= t_.retries) {
                    return fail(LockFault::BoltJam, true);
                }
                ++tries_;
                go(Phase::BoltRest, now_ms, t_.bolt_rest_ms);
            }
            break;

        case Phase::BoltRest:
            if (expired) {
                bolt_on_ = true;
                go(Phase::BoltPull, now_ms, t_.bolt_retract_ms);
            }
            break;

        case Phase::PreMove:
            if (!all_retracted(fb)) {
                go(Phase::BoltPull, now_ms, t_.bolt_retract_ms);  // a bolt fell back
            } else if (expired) {
                phase_ = Phase::Idle;
                return Result::Released;
            }
            break;

        case Phase::Settle:
            if (expired) {
                mag_on_ = true;
                go(Phase::MagBond, now_ms, t_.mag_bond_ms);
            }
            break;

        case Phase::MagBond: {
            const bool bonded = all_bonded(fb);
            if (bonded || expired) {
                mag_failed_ = !bonded;  // the bolts still go in
                bolt_on_ = false;
                go(Phase::BoltDrop, now_ms, t_.bolt_extend_ms);
            }
            break;
        }

        case Phase::BoltDrop:
            if (all_extended(fb)) {
                return finish_engage();
            }
            if (expired) {
                if (tries_ >= t_.retries) {
                    fault_ = LockFault::Incomplete;
                    phase_ = Phase::Idle;
                    return Result::Failed;
                }
                ++tries_;
                bolt_on_ = true;
                go(Phase::BoltRepull, now_ms, t_.repull_ms);
            }
            break;

        case Phase::BoltRepull:
            if (expired) {
                bolt_on_ = false;
                go(Phase::BoltDrop, now_ms, t_.bolt_extend_ms);
            }
            break;
    }
    return Result::None;
}

}  // namespace gate::state_machine
