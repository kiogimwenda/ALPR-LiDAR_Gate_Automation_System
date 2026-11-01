// virtual_operator.hpp — a Centurion-style operator with its power and
// current models, driven through the same pins as the real one.
// Header-only physics: FRX opens, TRG runs Standard mode, an open
// LCK/STP permit stops at once, and the beam reverses a close by itself.

#pragma once

#include <cstdint>

#include "gate_state_machine/gate_supervisor.hpp"

namespace gate::sim {

// 24 V lead-acid bank, spec 12. Thresholds are [assumed] there too.
class PowerModel {
public:
    double volts = 26.0;
    bool mains = true;

    void drain(double amps, std::uint32_t dt_ms) {
        if (!mains) {
            volts -= amps * dt_ms * kVoltsPerAmpMs;
        }
    }
    [[nodiscard]] state_machine::BatteryLevel level() const {
        if (volts < 21.6) {
            return state_machine::BatteryLevel::BelowCutoff;
        }
        return volts < 23.6 ? state_machine::BatteryLevel::BelowReserve
                            : state_machine::BatteryLevel::Ok;
    }
    [[nodiscard]] bool critical() const { return volts < 22.8; }

private:
    static constexpr double kVoltsPerAmpMs = 1e-5;
};

class VirtualOperator {
public:
    struct Config {
        std::uint32_t travel_ms = 4'000;
        std::uint32_t base_ma = 6'000;
        std::uint32_t inrush_ma = 14'000;
        std::uint32_t inrush_ms = 300;
    };

    struct Pin final : state_machine::OutputPin {
        VirtualOperator* op = nullptr;
        bool open_pin = false;
        bool level = false;
        void set(bool on) override {
            if (on && !level) {
                op->trigger(open_pin);
            }
            level = on;
        }
    };

    explicit VirtualOperator(Config cfg) : cfg_(cfg) {
        frx.op = this;
        frx.open_pin = true;
        trg.op = this;
    }

    Pin frx;
    Pin trg;

    void set_permit(bool ok) {
        permit_ = ok;
        if (!ok) {
            dir_ = 0;
        }
    }
    void set_beam_blocked(bool blocked) {
        if (blocked && dir_ < 0) {
            start(1);  // Safety Close: stop and open
        }
        beam_ = blocked;
    }
    void seize(bool on) { seized_ = on; }

    void tick(std::uint32_t dt_ms) {
        clock_ms_ += dt_ms;
        if (dir_ == 0 || seized_) {
            return;
        }
        run_ms_ += dt_ms;
        pos_ms_ += dir_ * static_cast<std::int64_t>(dt_ms);
        if (pos_ms_ >= cfg_.travel_ms) {
            pos_ms_ = cfg_.travel_ms;
            dir_ = 0;
        } else if (pos_ms_ <= 0) {
            pos_ms_ = 0;
            dir_ = 0;
        }
    }

    // Status LED: slow flash opening, fast closing, off closed, on open.
    [[nodiscard]] bool status_lit() const {
        if (dir_ > 0) {
            return clock_ms_ % 1'000 < 500;
        }
        if (dir_ < 0) {
            return clock_ms_ % 250 < 125;
        }
        return pos_ms_ > 0;
    }
    [[nodiscard]] std::uint32_t current_ma() const {
        if (dir_ == 0) {
            return 0;
        }
        return seized_ || run_ms_ < cfg_.inrush_ms ? cfg_.inrush_ma : cfg_.base_ma;
    }
    [[nodiscard]] bool at_closed() const { return pos_ms_ == 0; }
    [[nodiscard]] bool at_open() const { return pos_ms_ == cfg_.travel_ms; }
    [[nodiscard]] int direction() const { return dir_; }

private:
    void start(int d) {
        dir_ = d;
        run_ms_ = 0;
    }
    void trigger(bool open_pin) {
        if (!permit_) {
            return;
        }
        if (open_pin) {
            if (dir_ <= 0 && !at_open()) {
                start(1);  // FRX never closes
            }
        } else if (dir_ != 0) {
            dir_ = 0;  // Standard mode: moving -> stop
        } else if (!beam_) {
            start(at_closed() ? 1 : -1);
        }
    }

    Config cfg_;
    std::int64_t pos_ms_ = 0;
    std::uint32_t run_ms_ = 0;
    std::uint64_t clock_ms_ = 0;
    int dir_ = 0;
    bool permit_ = false;
    bool beam_ = false;
    bool seized_ = false;
};

}  // namespace gate::sim
