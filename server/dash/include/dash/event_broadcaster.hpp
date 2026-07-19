// event_broadcaster.hpp — server-side fan-out hub for DashboardEvents.
//
// Every interesting thing the server does (an AuthDecision, a firmware
// Telemetry tick, a FaultEvent, an OTA progress beat, a guard-issued
// GateCommand, a CommandAck) becomes a DashboardEvent and is published
// here. Subscribers — typically the gRPC DashboardService::Subscribe
// handler — receive the events on a per-subscription queue, filtered by
// the DashboardSubscription they registered with.
//
// Two queues:
//
//   1. The broadcaster's *ring buffer* (default 4096 events) is used for
//      replay-since-event-id. A reconnecting dashboard tells us its last
//      seen event_id and we replay any newer events that are still in
//      the ring before live events start flowing.
//
//   2. Each subscriber has a *per-subscription queue* (default 1024
//      events). Slow subscribers drop oldest rather than blocking the
//      publisher — the replay buffer covers gaps after reconnect.
//
// Threading: thread-safe by construction. publish() and subscribe() may
// be called from any thread; the broadcaster never holds locks across
// network I/O. stop() wakes every blocked subscriber and prevents
// further publishes.

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gate_service.pb.h"

namespace gate::dash {

// Routing metadata stored alongside each event so filtering doesn't
// have to peek into the payload's specific subtype.
struct EventEnvelope {
    std::string site_id;
    std::string gate_id;
    gate::v1::DashboardEvent event;
};

// True iff `event` matches the subscriber's `filter`. Pure helper —
// exposed for testing and reuse by the gRPC handler.
bool filter_matches(const gate::v1::DashboardSubscription& filter, const std::string& site_id,
                    const std::string& gate_id, const gate::v1::DashboardEvent& event);

class EventBroadcaster {
public:
    explicit EventBroadcaster(std::size_t ring_capacity = 4096,
                              std::size_t per_subscriber_capacity = 1024);
    ~EventBroadcaster();

    EventBroadcaster(const EventBroadcaster&) = delete;
    EventBroadcaster& operator=(const EventBroadcaster&) = delete;

    // Generic publish — assigns event_id, stamps event_ts (if unset), pushes
    // to the ring buffer, fans out to matching subscribers. Returns the
    // assigned event_id, or 0 if the broadcaster is stopped.
    std::uint64_t publish(std::string site_id, std::string gate_id, gate::v1::DashboardEvent event);

    // Convenience builders — wrap the payload, route via the proto-level
    // gate_id when present, and publish.
    std::uint64_t publish_decision(std::string site_id, gate::v1::AuthDecision d);
    std::uint64_t publish_telemetry(std::string site_id, gate::v1::Telemetry t);
    std::uint64_t publish_fault(std::string site_id, gate::v1::FaultEvent f);
    std::uint64_t publish_ota(std::string site_id, std::string gate_id, gate::v1::OtaProgress p);
    std::uint64_t publish_command(std::string site_id, gate::v1::GateCommand c);
    std::uint64_t publish_ack(std::string site_id, std::string gate_id, gate::v1::CommandAck a);

    class Subscription {
    public:
        ~Subscription();
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&&) noexcept;
        Subscription& operator=(Subscription&&) noexcept;

        // Block up to `timeout` for the next event. Returns nullopt on
        // timeout, after close(), or after broadcaster stop().
        std::optional<gate::v1::DashboardEvent> next(std::chrono::milliseconds timeout);

        // Drain everything currently queued, non-blocking. Returns empty
        // vector if no events are pending.
        std::vector<gate::v1::DashboardEvent> drain_now();

        // Detach from the broadcaster and wake any blocked next() call.
        void close();

        // Number of events dropped from this subscription's queue due
        // to over-capacity. Surfaced to clients via the replay path.
        std::uint64_t dropped_events() const;

    private:
        friend class EventBroadcaster;
        struct Impl;
        std::shared_ptr<Impl> impl_;
        explicit Subscription(std::shared_ptr<Impl> impl);
    };

    // Register a subscriber. Replays events from the ring buffer with
    // event_id > filter.since_event_id() that match the filter, then
    // streams subsequent matching events. Returns nullptr if stopped.
    std::unique_ptr<Subscription> subscribe(gate::v1::DashboardSubscription filter);

    // Wake every subscriber and reject further publishes.
    void stop();
    bool stopped() const;

    // Diagnostics.
    std::size_t subscriber_count() const;
    std::uint64_t last_event_id() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gate::dash
