// served_guard.hpp — late-grant and expiry checks, spec section 9.3.
// The ring is plain bytes so the caller can persist it to NVS unchanged.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gate::state_machine {

class ServedGuard {
public:
    static constexpr std::size_t kHistory = 32;
    using Id = std::array<std::uint8_t, 16>;
    struct Persisted {
        std::array<Id, kHistory> ids{};
        std::uint32_t next = 0;
    };
    enum class Verdict : std::uint8_t { Accept, AlreadyServed, Expired };

    // expires_unix_ms 0 = none. With an unsynced clock the expiry cannot
    // be judged, and arrival on the live stream is the only check.
    [[nodiscard]] Verdict check(const Id& id, std::int64_t expires_unix_ms,
                                std::int64_t now_unix_ms, bool clock_synced) const noexcept {
        if (seen(id)) {
            return Verdict::AlreadyServed;
        }
        if (expires_unix_ms != 0 && clock_synced && now_unix_ms > expires_unix_ms) {
            return Verdict::Expired;
        }
        return Verdict::Accept;
    }

    void mark_served(const Id& id) noexcept {
        p_.ids[p_.next % kHistory] = id;
        p_.next = (p_.next + 1) % kHistory;
    }

    [[nodiscard]] const Persisted& persisted() const noexcept { return p_; }
    void restore(const Persisted& p) noexcept { p_ = p; }

private:
    [[nodiscard]] bool seen(const Id& id) const noexcept {
        if (id == Id{}) {
            return false;  // legacy command: no request id
        }
        for (const Id& s : p_.ids) {
            if (s == id) {
                return true;
            }
        }
        return false;
    }
    Persisted p_{};
};

}  // namespace gate::state_machine
