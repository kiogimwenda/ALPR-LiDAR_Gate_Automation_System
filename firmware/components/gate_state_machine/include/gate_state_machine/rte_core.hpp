// rte_core.hpp — the logic of the exit-button service, free of ESP-IDF:
// admits OPEN commands, drives RteAuthority and the local close. The
// firmware's RteService adds only GPIO, tasks and envelope encoding.

#pragma once

#include <cstdint>

#include "gate_state_machine/gate_persist.hpp"
#include "gate_state_machine/rte_authority.hpp"

namespace gate::state_machine {

class RteCore {
public:
    struct Sample {
        std::uint64_t now_ms = 0;
        bool pressed = false;
        bool link_up = false;
        std::uint8_t wire_state = 0;  // GateState number
        ServedGuard::Id fresh_id{};
        EventRecord base{};
    };
    struct Step {
        RteAuthority::Output output = RteAuthority::Output::None;
        ServedGuard::Id id{};
        bool post_close = false;
    };

    RteCore(GatePersist& persist, EventRing* ring, std::uint32_t offline_close_ms,
            RteAuthority::Config cfg = {}) noexcept
        : persist_(persist), offline_close_ms_(offline_close_ms), authority_(cfg, ring, guard_) {
        ServedGuard::Persisted p;
        persist_.load_served(p);
        guard_.restore(p);
    }

    Step tick(const Sample& s) {
        // Wire numbers: spec 8.5.
        const std::uint8_t st = s.wire_state;
        if (st == 2 || st == 3 || st == 7) {  // opening, open, unlocking
            authority_.confirm_motion();
        }
        Step out;
        out.output = authority_.tick(s.now_ms, s.pressed, s.link_up, s.fresh_id, s.base);
        out.id = authority_.id();
        if (out.output == RteAuthority::Output::OpenLocal) {
            (void)persist_.save_served(guard_.persisted());
            closer_.arm(s.now_ms, offline_close_ms_);
        }
        // Closed, closed-insecure, lockdown, closing, locking.
        const bool shut = st == 1 || st == 9 || st == 6 || st == 4 || st == 8;
        out.post_close = closer_.tick(s.now_ms, shut);
        return out;
    }

    // A late or expired OPEN is refused; an accepted id is marked served.
    ServedGuard::Verdict admit_open(const ServedGuard::Id& id, std::int64_t expires_unix_ms,
                                    std::int64_t now_unix_ms, bool clock_synced) {
        const auto v = guard_.check(id, expires_unix_ms, now_unix_ms, clock_synced);
        if (v == ServedGuard::Verdict::Accept && id != ServedGuard::Id{}) {
            guard_.mark_served(id);
            (void)persist_.save_served(guard_.persisted());
        }
        return v;
    }

    void on_reply(RteAuthority::Reply r, const ServedGuard::Id& id, std::uint64_t now_ms) {
        if (id == authority_.id()) {
            authority_.on_reply(r, now_ms);
        }
    }

private:
    GatePersist& persist_;
    std::uint32_t offline_close_ms_;
    ServedGuard guard_;
    RteAuthority authority_;
    LocalCloser closer_;
};

}  // namespace gate::state_machine
