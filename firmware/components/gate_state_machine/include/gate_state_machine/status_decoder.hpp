// status_decoder.hpp — Centurion Status LED patterns, spec section 5.2.
// Feed one sample per 20 ms. Flash timing is [assumed] until bench test
// B1: slow flash on-time at least 300 ms, group flashes shorter, gaps
// between groups at least 800 ms.

#pragma once

#include <cstdint>

namespace gate::state_machine {

enum class OperatorStatus : std::uint8_t {
    Unknown,
    Closed,
    OpenOrPart,
    Opening,
    Closing,
    NoMains,
    LowBattery,
    MultiCollision,
};

class StatusDecoder {
public:
    static constexpr std::uint32_t kSampleMs = 20;
    static constexpr std::uint32_t kSteadyMs = 2'500;
    static constexpr std::uint32_t kGroupGapMs = 800;
    static constexpr std::uint32_t kSlowOnMs = 300;

    OperatorStatus sample(bool lit) noexcept {
        if (lit == lit_) {
            run_ms_ += kSampleMs;
        } else {
            if (lit_) {  // a pulse just ended
                ++pulses_;
                classify_pulse(run_ms_);
            }
            lit_ = lit;
            run_ms_ = kSampleMs;
        }
        if (!lit_ && run_ms_ >= kGroupGapMs && pulses_ > 0) {
            finish_group();
        }
        if (run_ms_ >= kGroupGapMs && moving()) {
            status_ = OperatorStatus::Unknown;  // the flashing stopped
        }
        if (run_ms_ >= kSteadyMs) {
            status_ = lit_ ? OperatorStatus::OpenOrPart : OperatorStatus::Closed;
        }
        return status_;
    }

    [[nodiscard]] OperatorStatus status() const noexcept { return status_; }

    // A new command starts a new verdict: an earlier run's flash must
    // not read as this run's direction.
    void forget_motion() noexcept {
        pulses_ = 0;
        if (moving()) {
            status_ = OperatorStatus::Unknown;
        }
    }

private:
    [[nodiscard]] bool moving() const noexcept {
        return status_ == OperatorStatus::Opening || status_ == OperatorStatus::Closing;
    }

    void classify_pulse(std::uint32_t on_ms) noexcept {
        if (on_ms >= kSlowOnMs && pulses_ >= 2) {
            status_ = OperatorStatus::Opening;
        } else if (on_ms < kSlowOnMs && pulses_ >= 5) {
            status_ = OperatorStatus::Closing;
        }
    }

    void finish_group() noexcept {
        if (status_ != OperatorStatus::Opening && status_ != OperatorStatus::Closing) {
            if (pulses_ == 2) {
                status_ = OperatorStatus::NoMains;
            } else if (pulses_ == 3) {
                status_ = OperatorStatus::LowBattery;
            } else if (pulses_ == 4) {
                status_ = OperatorStatus::MultiCollision;
            }  // 1 = pillar-light override, ignored
        }
        pulses_ = 0;
    }

    OperatorStatus status_ = OperatorStatus::Unknown;
    std::uint32_t run_ms_ = 0;
    std::uint32_t pulses_ = 0;
    bool lit_ = false;
};

}  // namespace gate::state_machine
