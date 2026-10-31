// gate_arbiter.cpp — see the header.

#include "rpc/gate_arbiter.hpp"

#include <algorithm>
#include <utility>

namespace gate::rpc {

using gate::v1::GATE_SIDE_EXIT;
using gate::v1::OpenReply;

GateArbiter::GateArbiter(Config cfg, GrantFn grant, AuditFn audit, RevalidateFn revalidate)
    : cfg_(cfg),
      grant_(std::move(grant)),
      audit_(std::move(audit)),
      revalidate_(std::move(revalidate)) {}

OpenReply GateArbiter::submit(const Request& r, std::uint64_t now_ms) {
    OpenReply reply;
    reply.set_request_id(r.request_id);
    if (served_.count(r.request_id) != 0) {
        reply.set_status(OpenReply::ALREADY_SERVED);
        return reply;
    }
    auto& queue = r.side == GATE_SIDE_EXIT ? exit_ : entry_;
    const auto same = std::find_if(queue.begin(), queue.end(), [&](const Waiter& w) {
        return !r.vehicle_key.empty() && w.request.vehicle_key == r.vehicle_key;
    });
    if (same != queue.end()) {
        same->since = std::max(same->since, now_ms);  // rule 9: refresh
        reply.set_status(OpenReply::QUEUED);
        reply.set_queue_position(static_cast<std::uint32_t>(same - queue.begin()) + 1);
        return reply;
    }
    queue.push_back({r, now_ms, {}});
    pump(now_ms);
    const bool waiting = std::any_of(queue.begin(), queue.end(), [&](const Waiter& w) {
        return w.request.request_id == r.request_id;
    });
    if (waiting) {
        reply.set_status(OpenReply::QUEUED);
        reply.set_queue_position(static_cast<std::uint32_t>(queue.size()));
    } else {
        reply.set_status(OpenReply::GRANTED);
    }
    return reply;
}

void GateArbiter::finish(const Waiter& w, const char* outcome, std::uint64_t now_ms) {
    if (audit_) {
        audit_({w.request, static_cast<std::uint32_t>(now_ms - w.since), w.preempted_by, outcome});
    }
}

void GateArbiter::grant(Waiter w, std::uint64_t now_ms) {
    if (grant_) {
        grant_(w.request);
    }
    finish(w, "granted", now_ms);
    current_ = Current{std::move(w), now_ms};
}

void GateArbiter::pump(std::uint64_t now_ms) {
    while (!entry_.empty() && now_ms - entry_.front().since >= cfg_.entry_ttl_ms) {
        finish(entry_.front(), "expired", now_ms);
        entry_.pop_front();
    }
    if (current_ && now_ms - current_->granted_at >= cfg_.pass_timeout_ms) {
        finish(current_->waiter, "abandoned", now_ms);  // rule 5
        current_.reset();
    }
    const bool two_lane = cfg_.lane_mode == gate::v1::LANE_MODE_TWO_LANE;
    if (two_lane) {
        // Sides are independent; the single close stays the firmware's.
        for (auto* q : {&exit_, &entry_}) {
            while (!q->empty()) {
                Waiter w = std::move(q->front());
                q->pop_front();
                grant(std::move(w), now_ms);
                current_.reset();
            }
        }
        return;
    }
    while (!current_ && !occupied_) {  // rule 1
        const bool entry_ready = !entry_.empty();
        const bool exit_first = !exit_.empty() && !(exit_streak_ >= cfg_.max_exit_streak &&
                                                    !entry_.empty());  // rules 2, 7
        if (exit_first) {
            Waiter w = std::move(exit_.front());
            exit_.pop_front();
            ++exit_streak_;
            if (!entry_.empty() && entry_.front().preempted_by.empty()) {
                entry_.front().preempted_by = w.request.request_id;  // rule 3
            }
            grant(std::move(w), now_ms);
        } else if (entry_ready) {
            Waiter w = std::move(entry_.front());
            entry_.pop_front();
            exit_streak_ = 0;
            if (revalidate_ && !revalidate_(w.request)) {
                finish(w, "refused", now_ms);  // rule 8
                continue;
            }
            grant(std::move(w), now_ms);
        } else {
            return;
        }
    }
}

void GateArbiter::tick(std::uint64_t now_ms) {
    pump(now_ms);
}

void GateArbiter::set_occupied(bool occupied, std::uint64_t now_ms) {
    occupied_ = occupied;
    pump(now_ms);
}

void GateArbiter::vehicle_cleared(std::uint64_t now_ms) {
    current_.reset();
    pump(now_ms);
}

bool GateArbiter::remove(std::deque<Waiter>& q, const std::string& id, std::uint64_t now_ms) {
    const auto it = std::find_if(q.begin(), q.end(),
                                 [&](const Waiter& w) { return w.request.request_id == id; });
    if (it == q.end()) {
        return false;
    }
    finish(*it, "served_offline", now_ms);
    q.erase(it);
    return true;
}

void GateArbiter::mark_served_offline(const std::string& request_id, std::uint64_t now_ms) {
    if (served_.insert(request_id).second) {
        served_order_.push_back(request_id);
        if (served_order_.size() > kMaxServed) {
            served_.erase(served_order_.front());
            served_order_.pop_front();
        }
    }
    if (!remove(exit_, request_id, now_ms) && !remove(entry_, request_id, now_ms) && current_ &&
        current_->waiter.request.request_id == request_id) {
        current_.reset();
    }
    pump(now_ms);
}

}  // namespace gate::rpc
