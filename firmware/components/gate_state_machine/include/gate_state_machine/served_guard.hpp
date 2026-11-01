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

    // UUID text <-> the ring's binary id. Anything else parses to the
    // zero id, which check() treats as a legacy command.
    static Id parse_uuid(const char* text) noexcept {
        Id out{};
        std::size_t n = 0;
        int high = -1;
        for (const char* p = text; *p != '\0'; ++p) {
            if (*p == '-') {
                continue;
            }
            const int v = nibble(*p);
            if (v < 0 || n >= out.size()) {
                return Id{};
            }
            if (high < 0) {
                high = v;
            } else {
                out[n++] = static_cast<std::uint8_t>(high << 4 | v);
                high = -1;
            }
        }
        return n == out.size() && high < 0 ? out : Id{};
    }
    static void format_uuid(const Id& id, char* out /* 37 bytes */) noexcept {
        static constexpr char kHex[] = "0123456789abcdef";
        std::size_t o = 0;
        for (std::size_t i = 0; i < id.size(); ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) {
                out[o++] = '-';
            }
            out[o++] = kHex[id[i] >> 4];
            out[o++] = kHex[id[i] & 15];
        }
        out[o] = '\0';
    }

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
    static int nibble(char c) noexcept {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    }
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
