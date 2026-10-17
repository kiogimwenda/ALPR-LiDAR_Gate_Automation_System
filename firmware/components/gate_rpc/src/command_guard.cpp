// command_guard.cpp — duplicate / stale command refusal.

#include "gate_rpc/command_guard.hpp"

#include <cstring>

namespace gate::rpc {

CommandGuard::Verdict CommandGuard::admit(const char* command_id, std::int64_t issued_unix_ms,
                                          std::int64_t now_unix_ms) noexcept {
    if (command_id == nullptr || command_id[0] == '\0') {
        return Verdict::kMissingId;
    }
    if (seen(command_id)) {
        return Verdict::kDuplicate;
    }
    Verdict v = Verdict::kAcceptUnchecked;
    if (issued_unix_ms > 0 && now_unix_ms > 0) {
        const std::int64_t age = now_unix_ms - issued_unix_ms;
        if (age > cfg_.max_age_ms) {
            return Verdict::kStale;
        }
        if (-age > cfg_.max_future_ms) {
            return Verdict::kFromFuture;
        }
        v = Verdict::kAccept;
    }
    remember(command_id);
    return v;
}

const char* CommandGuard::describe(Verdict v) noexcept {
    switch (v) {
        case Verdict::kAccept:
        case Verdict::kAcceptUnchecked:
            return "";
        case Verdict::kDuplicate:
            return "duplicate command_id — already executed";
        case Verdict::kStale:
            return "command is stale (issued too long ago) — not executed";
        case Verdict::kFromFuture:
            return "command issued_ts is ahead of the gate clock — check NTP";
        case Verdict::kMissingId:
            return "command has no command_id";
    }
    return "refused";
}

bool CommandGuard::seen(const char* command_id) const noexcept {
    for (const auto& id : ids_) {
        if (id[0] != '\0' && std::strncmp(id, command_id, kMaxCommandId - 1) == 0) {
            return true;
        }
    }
    return false;
}

void CommandGuard::remember(const char* command_id) noexcept {
    char* slot = ids_[next_];
    const std::size_t n = strnlen(command_id, kMaxCommandId - 1);
    std::memcpy(slot, command_id, n);
    slot[n] = '\0';
    next_ = (next_ + 1) % kHistory;
}

}  // namespace gate::rpc
