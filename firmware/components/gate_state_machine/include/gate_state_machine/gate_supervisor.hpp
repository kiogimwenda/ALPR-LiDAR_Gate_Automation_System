// gate_supervisor.hpp — executes lock-sequenced Actions against hardware
// interfaces, so firmware and host tests run the same logic. Rev 3
// drivers (I2C expanders, ADC) plug in behind these interfaces. Unused
// with LockLayout::None, where GateController keeps its relay path.

#pragma once

#include <array>
#include <cstdint>

#include "gate_state_machine/lock_sequencer.hpp"
#include "gate_state_machine/state_machine.hpp"
#include "gate_state_machine/status_decoder.hpp"

namespace gate::state_machine {

class OutputPin {
public:
    virtual ~OutputPin() = default;
    virtual void set(bool on) = 0;
};

class LockIo {
public:
    virtual ~LockIo() = default;
    virtual void drive(bool bolts, bool maglocks) = 0;
    [[nodiscard]] virtual LockFeedback feedback() = 0;
    virtual void set_permit(bool on) = 0;  // FW_PERMIT, into the hardware AND
    [[nodiscard]] virtual bool permit_readback() = 0;
};

class GateActuator {
public:
    virtual ~GateActuator() = default;
    virtual void open(std::uint64_t now_ms) = 0;
    virtual void close(std::uint64_t now_ms) = 0;
    virtual void stop() = 0;
    virtual void tick(std::uint64_t now_ms) = 0;
};

// Contactors held for the whole motion.
class ContactorActuator final : public GateActuator {
public:
    ContactorActuator(OutputPin& open, OutputPin& close) noexcept : open_(open), close_(close) {}
    void open(std::uint64_t) override {
        close_.set(false);
        open_.set(true);
    }
    void close(std::uint64_t) override {
        open_.set(false);
        close_.set(true);
    }
    void stop() override {
        open_.set(false);
        close_.set(false);
    }
    void tick(std::uint64_t) override {}

private:
    OutputPin& open_;
    OutputPin& close_;
};

// Commercial operator: 500 ms pulses on FRX (open) and TRG (close). The
// stop itself is the supervisor's LCK/STP permit drop.
class OperatorActuator final : public GateActuator {
public:
    static constexpr std::uint32_t kPulseMs = 500;
    OperatorActuator(OutputPin& frx, OutputPin& trg) noexcept : frx_(frx), trg_(trg) {}
    void open(std::uint64_t now_ms) override { pulse(frx_, now_ms); }
    void close(std::uint64_t now_ms) override { pulse(trg_, now_ms); }
    void stop() override { release(); }
    void tick(std::uint64_t now_ms) override {
        if (end_ms_ != 0 && now_ms >= end_ms_) {
            release();
        }
    }

private:
    void pulse(OutputPin& pin, std::uint64_t now_ms) {
        release();
        pin.set(true);
        end_ms_ = now_ms + kPulseMs;
    }
    void release() {
        frx_.set(false);
        trg_.set(false);
        end_ms_ = 0;
    }
    OutputPin& frx_;
    OutputPin& trg_;
    std::uint64_t end_ms_ = 0;
};

class GateSupervisor {
public:
    struct Config {
        LockLayout layout = LockLayout::Four;
        ActuatorProfile profile = ActuatorProfile::Operator;
        LockTimingConfig timing{};
        std::uint32_t op_start_timeout_ms = 2'000;
        std::uint32_t reversal_guard_ms = 3'000;
        std::uint32_t beam_quiet_ms = 3'000;  // no direction verdict after a trip
        std::uint32_t permit_check_ms = 100;
        std::uint32_t permit_rearm_ms = 200;
        std::uint32_t bolt_hold_open_ms = 45'000;
        std::uint32_t relock_retry_ms = 60'000;
        std::uint32_t relock_tries = 5;
    };
    struct Events {
        std::array<Event, 4> items{};
        std::size_t count = 0;
    };

    GateSupervisor(Config cfg, LockIo& locks, GateActuator& actuator) noexcept;

    // Every event passes here first, to see beam trips and human stops.
    void observe(Event ev, std::uint64_t now_ms) noexcept;
    void apply(const Outputs& out, std::uint64_t now_ms) noexcept;
    // Verify and calibration are not implemented yet: refuse them here
    // so neither procedure can reach the state machine.
    [[nodiscard]] static bool admits(Event ev) noexcept {
        return ev != Event::BeginVerify && ev != Event::BeginCalibrate;
    }
    // Call every 20 ms; post the returned events to the state machine.
    Events tick(std::uint64_t now_ms, bool status_lit) noexcept;

    [[nodiscard]] bool locks_proven() noexcept;
    void assume_engaged() noexcept;
    void start_relock(std::uint64_t now_ms) noexcept { sequencer_.start_engage(now_ms); }
    [[nodiscard]] OperatorStatus status() const noexcept { return decoder_.status(); }

private:
    enum class Motion : std::uint8_t { None, Open, Close };

    void command(Motion m, std::uint64_t now_ms) noexcept;
    void stop(std::uint64_t now_ms) noexcept;
    void track_state(const Outputs& out, std::uint64_t now_ms) noexcept;
    void check_operator(Events& e, std::uint64_t now_ms) noexcept;
    void drive(Motion m, std::uint64_t now_ms) noexcept;
    void clear_permit(std::uint64_t now_ms) noexcept;
    void rest_bolts() noexcept;
    [[nodiscard]] bool guarded(std::uint64_t now_ms) const noexcept {
        return now_ms < guard_until_ && !human_stop_;
    }
    [[nodiscard]] bool operator_profile() const noexcept {
        return cfg_.profile == ActuatorProfile::Operator;
    }

    Config cfg_;
    LockIo& locks_;
    GateActuator& actuator_;
    LockSequencer sequencer_;
    StatusDecoder decoder_;
    State state_ = State::Initializing;
    Motion motion_ = Motion::None;
    bool permit_wanted_ = false;
    bool started_ = false;  // operator confirmed moving this run
    bool retried_ = false;
    bool human_stop_ = false;  // this step overrides the reversal guard
    bool key_active_ = false;
    bool rested_ = false;            // hold timer dropped the bolts; re-prove before motion
    Motion pending_ = Motion::None;  // drive waiting on that re-proof
    bool deferred_stop_ = false;
    bool deferred_clear_ = false;
    bool deferred_rest_ = false;
    Event refused_ = Event::CommandStop;  // CommandStop = nothing refused
    std::uint64_t start_deadline_ = 0;
    std::uint64_t quiet_until_ = 0;
    std::uint64_t guard_until_ = 0;
    std::uint64_t permit_check_at_ = 0;
    std::uint64_t rearm_at_ = 0;
    std::uint64_t hold_until_ = 0;  // cumulative across a stop
    std::uint64_t relock_at_ = 0;
    std::uint32_t relocks_ = 0;
};

}  // namespace gate::state_machine
