// rte_authority.hpp — the exit button's local authority, spec 9.3. Pure:
// feed it debounced-rate samples, server replies and link state. When
// the server cannot answer in time, the button opens on its own, logged
// durably first and marked served so a late grant cannot cycle the gate.

#pragma once

#include <cstdint>

#include "gate_state_machine/event_ring.hpp"
#include "gate_state_machine/served_guard.hpp"

namespace gate::state_machine {

class RteAuthority {
public:
    struct Config {
        std::uint32_t debounce_ms = 50;
        std::uint32_t rate_limit_ms = 3'000;
        std::uint32_t reset_hold_ms = 5'000;
        std::uint32_t stuck_hold_ms = 10'000;
        std::uint32_t grant_timeout_ms = 1'500;
        std::uint32_t queue_max_ms = 20'000;
        std::uint32_t confirm_ms = 3'000;  // a grant must show motion by then
    };
    enum class Output : std::uint8_t {
        None,
        SendRequest,  // send OpenRequest with id()
        OpenLocal,    // post CommandOpen with OpenOrigin::Rte
        LocalReset,   // post Event::LocalReset
        Stuck,        // raise RTE_STUCK
    };
    enum class Reply : std::uint8_t { Granted, Queued, Denied, AlreadyServed };

    // `ring` may be null: the exit still opens, unlogged (spec 9.4).
    RteAuthority(Config cfg, EventRing* ring, ServedGuard& guard) noexcept
        : cfg_(cfg), ring_(ring), guard_(guard) {}

    // `fresh_id` is used if this sample starts a new request.
    Output tick(std::uint64_t now_ms, bool pressed, bool link_up, const ServedGuard::Id& fresh_id,
                EventRecord base) {
        Output out = track_button(now_ms, pressed, link_up, fresh_id);
        if (out == Output::None && waiting_ && (!link_up || now_ms >= deadline_)) {
            out = serve_local(base);
        } else if (out == Output::OpenLocal) {
            out = serve_local(base);
        }
        return out;
    }

    void on_reply(Reply r, std::uint64_t now_ms) noexcept {
        if (!waiting_) {
            return;
        }
        if (r == Reply::Queued) {
            deadline_ = now_ms + cfg_.queue_max_ms;
        } else if (r == Reply::Denied) {
            deadline_ = now_ms;  // the exit always opens
        } else {
            // Not served until the gate moves: a lost command must not
            // lose the exit.
            deadline_ = now_ms + cfg_.confirm_ms;
        }
    }

    // The gate is opening or open: whatever was asked for has happened.
    void confirm_motion() noexcept { waiting_ = false; }

    [[nodiscard]] const ServedGuard::Id& id() const noexcept { return id_; }

private:
    Output track_button(std::uint64_t now_ms, bool pressed, bool link_up,
                        const ServedGuard::Id& fresh_id) noexcept {
        if (!pressed) {
            down_ = false;
            fired_reset_ = false;
            stuck_ = false;
            return Output::None;
        }
        if (!down_) {
            down_ = true;
            down_since_ = now_ms;
            pressed_ = false;
            return Output::None;
        }
        const std::uint64_t held = now_ms - down_since_;
        if (held >= cfg_.stuck_hold_ms) {
            if (!stuck_) {
                stuck_ = true;
                return Output::Stuck;
            }
            return Output::None;
        }
        if (held >= cfg_.reset_hold_ms && !fired_reset_) {
            fired_reset_ = true;
            return Output::LocalReset;
        }
        if (held >= cfg_.debounce_ms && !pressed_) {
            pressed_ = true;
            if (armed_ && now_ms < next_allowed_) {
                return Output::None;
            }
            armed_ = true;
            next_allowed_ = now_ms + cfg_.rate_limit_ms;
            id_ = fresh_id;
            if (!link_up) {
                return Output::OpenLocal;
            }
            waiting_ = true;
            deadline_ = now_ms + cfg_.grant_timeout_ms;
            return Output::SendRequest;
        }
        return Output::None;
    }

    Output serve_local(EventRecord r) {
        waiting_ = false;
        r.kind = kEventOfflineOpen;
        r.request_id = id_;
        guard_.mark_served(id_);
        if (ring_ != nullptr) {
            ring_->append(r);  // before the command runs
        }
        return Output::OpenLocal;
    }

    Config cfg_;
    EventRing* ring_;
    ServedGuard& guard_;
    ServedGuard::Id id_{};
    std::uint64_t down_since_ = 0;
    std::uint64_t deadline_ = 0;
    std::uint64_t next_allowed_ = 0;
    bool armed_ = false;
    bool down_ = false;
    bool pressed_ = false;
    bool waiting_ = false;
    bool fired_reset_ = false;
    bool stuck_ = false;
};

// Local auto-close after an offline exit. Retries until the gate is shut
// or closing, because the beam can refuse any one attempt.
class LocalCloser {
public:
    static constexpr std::uint32_t kRetryMs = 10'000;

    void arm(std::uint64_t now_ms, std::uint32_t delay_ms) noexcept { at_ = now_ms + delay_ms; }
    void cancel() noexcept { at_ = 0; }
    [[nodiscard]] bool armed() const noexcept { return at_ != 0; }

    // True when the caller should post a close now.
    bool tick(std::uint64_t now_ms, bool shut_or_closing) noexcept {
        if (at_ == 0 || now_ms < at_) {
            return false;
        }
        if (shut_or_closing) {
            at_ = 0;
            return false;
        }
        at_ = now_ms + kRetryMs;
        return true;
    }

private:
    std::uint64_t at_ = 0;
};

}  // namespace gate::state_machine
