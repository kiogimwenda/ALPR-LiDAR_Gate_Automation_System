// gate_arbiter.hpp — one gate's open-request arbitration, spec section 10.
// Decides when, never whether: allow/deny stays in fusion. Pure and
// clock-injected; the caller serialises access and publishes grants.

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <string>

#include "gate_service.pb.h"

namespace gate::rpc {

class GateArbiter {
public:
    struct Config {
        gate::v1::LaneMode lane_mode = gate::v1::LANE_MODE_SHARED;
        std::uint32_t pass_timeout_ms = 30'000;
        std::uint32_t entry_ttl_ms = 120'000;
        std::uint32_t max_exit_streak = 3;
    };
    struct Request {
        std::string request_id;
        gate::v1::GateSide side = gate::v1::GATE_SIDE_ENTRY;
        gate::v1::OpenSource source = gate::v1::OPEN_SOURCE_UNSPECIFIED;
        std::string vehicle_key;  // plate; dedupes repeat reads per side
        std::string command_id;   // carried to the published command
        std::string actor;
        std::string decision_id;
        std::string pass_id;
    };
    struct Audit {
        Request request;
        std::uint32_t queued_ms = 0;
        std::string preempted_by;
        std::string outcome;  // granted, abandoned, expired, served_offline, refused
    };
    static constexpr std::size_t kMaxServed = 256;
    using GrantFn = std::function<void(const Request&)>;
    using AuditFn = std::function<void(const Audit&)>;
    // Rule 8: a queued entry is rechecked against block and lockdown.
    using RevalidateFn = std::function<bool(const Request&)>;

    GateArbiter(Config cfg, GrantFn grant, AuditFn audit, RevalidateFn revalidate);

    gate::v1::OpenReply submit(const Request& r, std::uint64_t now_ms);
    void tick(std::uint64_t now_ms);
    void set_occupied(bool occupied, std::uint64_t now_ms);
    void vehicle_cleared(std::uint64_t now_ms);  // current grant finished
    // The controller opened on local authority: cancel any pending grant.
    // Rule 6: LiDAR says this vehicle left the queue.
    void withdraw(const std::string& vehicle_key, std::uint64_t now_ms);
    // Rule 6: a live waiter or grant holds off auto-close.
    [[nodiscard]] bool hold_auto_close() const noexcept { return granted() || waiting() > 0; }
    void mark_served_offline(const std::string& request_id, std::uint64_t now_ms);

    [[nodiscard]] std::size_t waiting() const noexcept { return entry_.size() + exit_.size(); }
    [[nodiscard]] bool granted() const noexcept { return current_.has_value(); }

private:
    struct Waiter {
        Request request;
        std::uint64_t since = 0;      // first seen: drives queued_ms and the TTL
        std::uint64_t last_seen = 0;  // refreshed by repeat reads
        std::string preempted_by;
    };
    struct Current {
        Waiter waiter;
        std::uint64_t granted_at = 0;
    };

    void pump(std::uint64_t now_ms);
    void grant(Waiter w, std::uint64_t now_ms);
    void finish(const Waiter& w, const char* outcome, std::uint64_t now_ms);
    void remember(const std::string& id);
    bool remove(std::deque<Waiter>& q, const std::string& id, std::uint64_t now_ms);

    Config cfg_;
    GrantFn grant_;
    AuditFn audit_;
    RevalidateFn revalidate_;
    std::deque<Waiter> entry_;
    std::deque<Waiter> exit_;
    std::optional<Current> current_;
    std::set<std::string> served_;
    std::deque<std::string> served_order_;  // evicts the oldest past kMaxServed
    std::uint32_t exit_streak_ = 0;
    bool occupied_ = false;
};

}  // namespace gate::rpc
