// event_broadcaster.cpp

#include "dash/event_broadcaster.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <list>
#include <mutex>
#include <utility>

namespace gate::dash {

using gate::v1::DashboardEvent;
using gate::v1::DashboardSubscription;

bool filter_matches(const DashboardSubscription& filter, const std::string& site_id,
                    const std::string& gate_id, const DashboardEvent& event) {
    // 1. Site/gate scope. Empty fields mean "match anything."
    if (!filter.site_id().empty() && filter.site_id() != site_id)
        return false;
    if (filter.gate_ids_size() > 0) {
        bool found = false;
        for (int i = 0; i < filter.gate_ids_size(); ++i) {
            if (filter.gate_ids(i) == gate_id) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }

    // 2. Kind flags. The proto only carries include_* flags for the four
    //    sensor/audit-data kinds. GateCommand and CommandAck (guard
    //    actions) are always included when a kind filter is in effect.
    //    A subscription with all flags false is treated as "everything"
    //    so a defaulted DashboardSubscription doesn't silently drop all.
    const bool any_kind_set = filter.include_decisions() || filter.include_telemetry() ||
                              filter.include_faults() || filter.include_ota();
    if (!any_kind_set)
        return true;

    switch (event.payload_case()) {
        case DashboardEvent::kDecision:
            return filter.include_decisions();
        case DashboardEvent::kTelemetry:
            return filter.include_telemetry();
        case DashboardEvent::kFault:
            return filter.include_faults();
        case DashboardEvent::kOta:
            return filter.include_ota();
        case DashboardEvent::kCommand:
        case DashboardEvent::kAck:
            return true;  // Always show guard actions when filtering.
        case DashboardEvent::PAYLOAD_NOT_SET:
            return false;
    }
    return false;
}

// --- Subscription::Impl -----------------------------------------------------

struct EventBroadcaster::Subscription::Impl {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<DashboardEvent> queue;
    DashboardSubscription filter;
    std::size_t capacity = 1024;
    std::uint64_t dropped = 0;
    bool closed = false;

    void push_locked(const DashboardEvent& e) {
        // capacity 0 means unbounded — used only by tests.
        if (capacity > 0 && queue.size() >= capacity) {
            queue.pop_front();
            ++dropped;
        }
        queue.push_back(e);
    }
};

EventBroadcaster::Subscription::Subscription(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
EventBroadcaster::Subscription::Subscription(Subscription&&) noexcept = default;
EventBroadcaster::Subscription& EventBroadcaster::Subscription::operator=(Subscription&&) noexcept =
    default;

EventBroadcaster::Subscription::~Subscription() {
    if (impl_)
        close();
}

std::optional<DashboardEvent> EventBroadcaster::Subscription::next(
    std::chrono::milliseconds timeout) {
    if (!impl_)
        return std::nullopt;
    std::unique_lock lk{impl_->mu};
    impl_->cv.wait_for(lk, timeout, [&] { return impl_->closed || !impl_->queue.empty(); });
    if (impl_->queue.empty())
        return std::nullopt;
    auto e = std::move(impl_->queue.front());
    impl_->queue.pop_front();
    return e;
}

std::vector<DashboardEvent> EventBroadcaster::Subscription::drain_now() {
    if (!impl_)
        return {};
    std::lock_guard lk{impl_->mu};
    std::vector<DashboardEvent> out;
    out.reserve(impl_->queue.size());
    while (!impl_->queue.empty()) {
        out.push_back(std::move(impl_->queue.front()));
        impl_->queue.pop_front();
    }
    return out;
}

void EventBroadcaster::Subscription::close() {
    if (!impl_)
        return;
    {
        std::lock_guard lk{impl_->mu};
        impl_->closed = true;
    }
    impl_->cv.notify_all();
}

std::uint64_t EventBroadcaster::Subscription::dropped_events() const {
    if (!impl_)
        return 0;
    std::lock_guard lk{impl_->mu};
    return impl_->dropped;
}

// --- EventBroadcaster::Impl -------------------------------------------------

struct EventBroadcaster::Impl {
    mutable std::mutex mu;
    std::deque<EventEnvelope> ring;
    std::size_t ring_capacity;
    std::size_t per_sub_capacity;
    std::uint64_t next_event_id = 1;
    std::list<std::weak_ptr<Subscription::Impl>> subs;
    std::atomic<bool> stopped{false};

    Impl(std::size_t rc, std::size_t pc) : ring_capacity(rc), per_sub_capacity(pc) {}
};

EventBroadcaster::EventBroadcaster(std::size_t ring_capacity, std::size_t per_subscriber_capacity)
    : impl_(std::make_unique<Impl>(ring_capacity, per_subscriber_capacity)) {}

EventBroadcaster::~EventBroadcaster() {
    stop();
}

bool EventBroadcaster::stopped() const {
    return impl_->stopped.load();
}

void EventBroadcaster::stop() {
    if (impl_->stopped.exchange(true))
        return;
    std::list<std::shared_ptr<Subscription::Impl>> alive;
    {
        std::lock_guard lk{impl_->mu};
        for (auto& w : impl_->subs) {
            if (auto s = w.lock())
                alive.push_back(std::move(s));
        }
        impl_->subs.clear();
    }
    for (auto& s : alive) {
        {
            std::lock_guard sl{s->mu};
            s->closed = true;
        }
        s->cv.notify_all();
    }
}

std::size_t EventBroadcaster::subscriber_count() const {
    std::lock_guard lk{impl_->mu};
    std::size_t n = 0;
    for (const auto& w : impl_->subs) {
        if (!w.expired())
            ++n;
    }
    return n;
}

std::uint64_t EventBroadcaster::last_event_id() const {
    std::lock_guard lk{impl_->mu};
    return impl_->next_event_id - 1;
}

std::uint64_t EventBroadcaster::publish(std::string site_id, std::string gate_id,
                                        DashboardEvent event) {
    if (impl_->stopped.load())
        return 0;

    std::list<std::shared_ptr<Subscription::Impl>> targets;
    EventEnvelope env;
    {
        std::lock_guard lk{impl_->mu};
        const auto id = impl_->next_event_id++;
        event.set_event_id(id);
        if (!event.has_event_ts() || event.event_ts().seconds() == 0) {
            event.mutable_event_ts()->set_seconds(std::time(nullptr));
        }
        env.site_id = std::move(site_id);
        env.gate_id = std::move(gate_id);
        env.event = std::move(event);
        if (impl_->ring.size() == impl_->ring_capacity)
            impl_->ring.pop_front();
        impl_->ring.push_back(env);

        // Snapshot live subscribers under the broadcaster mutex; drop dead
        // weak_ptrs in the same pass. Hand off shared_ptrs so the per-sub
        // notify happens outside this lock.
        for (auto it = impl_->subs.begin(); it != impl_->subs.end();) {
            auto s = it->lock();
            if (!s) {
                it = impl_->subs.erase(it);
            } else {
                targets.push_back(std::move(s));
                ++it;
            }
        }
    }

    for (auto& s : targets) {
        bool was_empty = false;
        {
            std::lock_guard sl{s->mu};
            if (s->closed)
                continue;
            if (!filter_matches(s->filter, env.site_id, env.gate_id, env.event))
                continue;
            was_empty = s->queue.empty();
            s->push_locked(env.event);
        }
        if (was_empty)
            s->cv.notify_one();
    }
    return env.event.event_id();
}

std::uint64_t EventBroadcaster::publish_decision(std::string site_id, gate::v1::AuthDecision d) {
    DashboardEvent e;
    std::string gate_id = d.gate_id();
    *e.mutable_decision() = std::move(d);
    return publish(std::move(site_id), std::move(gate_id), std::move(e));
}

std::uint64_t EventBroadcaster::publish_telemetry(std::string site_id, gate::v1::Telemetry t) {
    DashboardEvent e;
    std::string gate_id = t.gate_id();
    *e.mutable_telemetry() = std::move(t);
    return publish(std::move(site_id), std::move(gate_id), std::move(e));
}

std::uint64_t EventBroadcaster::publish_fault(std::string site_id, gate::v1::FaultEvent f) {
    DashboardEvent e;
    std::string gate_id = f.gate_id();
    *e.mutable_fault() = std::move(f);
    return publish(std::move(site_id), std::move(gate_id), std::move(e));
}

std::uint64_t EventBroadcaster::publish_ota(std::string site_id, std::string gate_id,
                                            gate::v1::OtaProgress p) {
    DashboardEvent e;
    *e.mutable_ota() = std::move(p);
    return publish(std::move(site_id), std::move(gate_id), std::move(e));
}

std::uint64_t EventBroadcaster::publish_command(std::string site_id, gate::v1::GateCommand c) {
    DashboardEvent e;
    std::string gate_id = c.gate_id();
    *e.mutable_command() = std::move(c);
    return publish(std::move(site_id), std::move(gate_id), std::move(e));
}

std::uint64_t EventBroadcaster::publish_ack(std::string site_id, std::string gate_id,
                                            gate::v1::CommandAck a) {
    DashboardEvent e;
    *e.mutable_ack() = std::move(a);
    return publish(std::move(site_id), std::move(gate_id), std::move(e));
}

std::unique_ptr<EventBroadcaster::Subscription> EventBroadcaster::subscribe(
    DashboardSubscription filter) {
    if (impl_->stopped.load())
        return nullptr;

    auto sub_impl = std::make_shared<Subscription::Impl>();
    sub_impl->filter = std::move(filter);
    sub_impl->capacity = impl_->per_sub_capacity;

    {
        std::lock_guard lk{impl_->mu};
        // Replay matching events with id > since_event_id.
        const auto since = sub_impl->filter.since_event_id();
        for (const auto& env : impl_->ring) {
            if (env.event.event_id() <= since)
                continue;
            if (!filter_matches(sub_impl->filter, env.site_id, env.gate_id, env.event))
                continue;
            // Hold the per-sub mutex briefly even though no one else has
            // a handle yet — keeps the invariant uniform.
            std::lock_guard sl{sub_impl->mu};
            sub_impl->push_locked(env.event);
        }
        impl_->subs.push_back(std::weak_ptr<Subscription::Impl>{sub_impl});
    }

    return std::unique_ptr<Subscription>{new Subscription{std::move(sub_impl)}};
}

}  // namespace gate::dash
