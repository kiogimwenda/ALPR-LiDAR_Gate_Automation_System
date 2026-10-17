// command_guard.hpp — replay protection for incoming GateCommands (pure C++20).
//
// The Control stream must never make a gate re-execute a command it
// already ran or one that has gone stale while the link was down. A
// stale OPEN_GATE opens a gate nobody is waiting at; a replayed REBOOT
// becomes a reboot loop. The server stops replaying history to
// reconnecting gates (FieldControllerServiceImpl::Control subscribes
// from "now"), and this guard is the firmware-side half of that
// defence, so neither side alone has to be right:
//
//   * duplicate command_id  — refused (ring of the last kHistory ids);
//   * issued_ts too old     — refused (older than max_age_ms);
//   * issued_ts too far in the future — refused (clock skew beyond
//     max_future_ms means one of the two clocks is wrong);
//   * no command_id         — refused (the ack could not be correlated).
//
// Age checks need both clocks. Until SNTP has synced (`now_unix_ms` 0)
// or when the server omitted issued_ts (`issued_unix_ms` 0) the guard
// still dedupes but cannot judge age, and says so in the verdict.
//
// No FreeRTOS/nanopb here — host-tested with Catch2 like
// command_tracker. ControlClient owns one guard under its mutex.

#pragma once

#include <cstddef>
#include <cstdint>

namespace gate::rpc {

class CommandGuard {
public:
    static constexpr std::size_t kHistory = 32;
    static constexpr std::size_t kMaxCommandId = 40;  // UUIDv4 + NUL (proto options)

    struct Config {
        std::int64_t max_age_ms = 30'000;     // a command older than this is stale
        std::int64_t max_future_ms = 30'000;  // tolerated server-ahead clock skew
    };

    enum class Verdict : std::uint8_t {
        kAccept,           // fresh and new — execute it
        kAcceptUnchecked,  // new, but age unknown (no wall clock / no issued_ts)
        kDuplicate,        // command_id already seen
        kStale,            // issued too long ago
        kFromFuture,       // issued too far ahead of the local clock
        kMissingId,        // empty command_id
    };

    CommandGuard() noexcept : CommandGuard(Config{}) {}
    explicit CommandGuard(Config cfg) noexcept : cfg_(cfg) {}

    // Decide whether `command_id` may execute. Unix-epoch milliseconds;
    // pass 0 for an unknown time. An accepted id is remembered; refused
    // ids are not (a stale command is not "seen" — the server may
    // legitimately reissue it with a fresh id and timestamp).
    Verdict admit(const char* command_id, std::int64_t issued_unix_ms,
                  std::int64_t now_unix_ms) noexcept;

    [[nodiscard]] static bool accepted(Verdict v) noexcept {
        return v == Verdict::kAccept || v == Verdict::kAcceptUnchecked;
    }

    // Human-readable refusal reason for the CommandAck error_text.
    [[nodiscard]] static const char* describe(Verdict v) noexcept;

private:
    bool seen(const char* command_id) const noexcept;
    void remember(const char* command_id) noexcept;

    Config cfg_;
    char ids_[kHistory][kMaxCommandId] = {};
    std::size_t next_ = 0;
};

}  // namespace gate::rpc
