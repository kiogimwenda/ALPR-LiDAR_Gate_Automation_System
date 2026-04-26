// event_broadcaster_test.cpp — fan-out hub unit tests.
//
// These cover the broadcaster's three contracts:
//   1. monotonic event_id assignment + event_ts stamping
//   2. filter semantics (site / gate / kind flags / replay)
//   3. backpressure (per-subscriber drop-oldest, ring eviction)
//
// All tests are single-process and use deterministic small queues so
// the back-pressure paths are exercised quickly.

#include "dash/event_broadcaster.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

using gate::dash::EventBroadcaster;
using gate::dash::filter_matches;
using gate::v1::AuthDecision;
using gate::v1::AuthVerdict;
using gate::v1::CommandAck;
using gate::v1::CommandKind;
using gate::v1::DashboardEvent;
using gate::v1::DashboardSubscription;
using gate::v1::FaultEvent;
using gate::v1::FaultSeverity;
using gate::v1::GateCommand;
using gate::v1::OtaProgress;
using gate::v1::Telemetry;

namespace {

constexpr auto kShortTimeout = std::chrono::milliseconds{50};
constexpr auto kLongTimeout = std::chrono::milliseconds{500};

AuthDecision make_decision(const std::string& gate_id, AuthVerdict v) {
    AuthDecision d;
    d.set_gate_id(gate_id);
    d.set_verdict(v);
    return d;
}

Telemetry make_telemetry(const std::string& gate_id) {
    Telemetry t;
    t.set_gate_id(gate_id);
    t.set_uptime_sec(42);
    return t;
}

FaultEvent make_fault(const std::string& gate_id, const std::string& code) {
    FaultEvent f;
    f.set_gate_id(gate_id);
    f.set_code(code);
    f.set_severity(FaultSeverity::FAULT_SEVERITY_ERROR);
    return f;
}

}  // namespace

TEST_CASE("EventBroadcaster: assigns monotonic event_id and stamps event_ts", "[dash][publish]") {
    EventBroadcaster bus;
    const auto id1 =
        bus.publish_decision("site-a", make_decision("g1", AuthVerdict::AUTH_VERDICT_AUTHORIZED));
    const auto id2 = bus.publish_telemetry("site-a", make_telemetry("g1"));
    const auto id3 = bus.publish_fault("site-a", make_fault("g1", "MOTOR_TIMEOUT"));

    REQUIRE(id1 == 1);
    REQUIRE(id2 == 2);
    REQUIRE(id3 == 3);
    REQUIRE(bus.last_event_id() == 3);

    // Replay everything via a fresh subscription with since=0.
    DashboardSubscription filter;
    auto sub = bus.subscribe(filter);
    REQUIRE(sub != nullptr);
    auto events = sub->drain_now();
    REQUIRE(events.size() == 3);
    for (const auto& e : events) {
        REQUIRE(e.event_id() > 0);
        REQUIRE(e.event_ts().seconds() > 0);
    }
}

TEST_CASE("EventBroadcaster: subscriber receives live events after subscribing", "[dash][live]") {
    EventBroadcaster bus;
    DashboardSubscription filter;
    auto sub = bus.subscribe(filter);

    bus.publish_decision("site-a", make_decision("g1", AuthVerdict::AUTH_VERDICT_AUTHORIZED));
    auto e = sub->next(kLongTimeout);
    REQUIRE(e.has_value());
    REQUIRE(e->payload_case() == DashboardEvent::kDecision);
    REQUIRE(e->decision().gate_id() == "g1");
}

TEST_CASE("EventBroadcaster: filter by site_id excludes other sites", "[dash][filter]") {
    EventBroadcaster bus;
    DashboardSubscription filter;
    filter.set_site_id("site-a");
    auto sub = bus.subscribe(filter);

    bus.publish_telemetry("site-a", make_telemetry("g1"));
    bus.publish_telemetry("site-b", make_telemetry("g1"));
    bus.publish_telemetry("site-a", make_telemetry("g2"));

    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 2);
    for (const auto& e : evs) {
        REQUIRE(e.payload_case() == DashboardEvent::kTelemetry);
    }
}

TEST_CASE("EventBroadcaster: filter by gate_ids restricts to listed gates", "[dash][filter]") {
    EventBroadcaster bus;
    DashboardSubscription filter;
    filter.add_gate_ids("g1");
    filter.add_gate_ids("g3");
    auto sub = bus.subscribe(filter);

    bus.publish_telemetry("site-a", make_telemetry("g1"));  // kept
    bus.publish_telemetry("site-a", make_telemetry("g2"));  // dropped
    bus.publish_telemetry("site-a", make_telemetry("g3"));  // kept
    bus.publish_telemetry("site-a", make_telemetry("g4"));  // dropped

    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 2);
    REQUIRE(evs[0].telemetry().gate_id() == "g1");
    REQUIRE(evs[1].telemetry().gate_id() == "g3");
}

TEST_CASE("EventBroadcaster: kind flags drop excluded payloads", "[dash][filter]") {
    EventBroadcaster bus;
    DashboardSubscription filter;
    filter.set_include_decisions(true);  // any flag set → kind filter active
    filter.set_include_faults(true);
    auto sub = bus.subscribe(filter);

    bus.publish_decision("site-a", make_decision("g1", AuthVerdict::AUTH_VERDICT_AUTHORIZED));
    bus.publish_telemetry("site-a", make_telemetry("g1"));  // dropped
    bus.publish_fault("site-a", make_fault("g1", "X"));

    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 2);
    REQUIRE(evs[0].payload_case() == DashboardEvent::kDecision);
    REQUIRE(evs[1].payload_case() == DashboardEvent::kFault);
}

TEST_CASE("EventBroadcaster: defaulted subscription includes all kinds", "[dash][filter]") {
    EventBroadcaster bus;
    auto sub = bus.subscribe(DashboardSubscription{});

    bus.publish_decision("a", make_decision("g", AuthVerdict::AUTH_VERDICT_AUTHORIZED));
    bus.publish_telemetry("a", make_telemetry("g"));
    bus.publish_fault("a", make_fault("g", "X"));
    OtaProgress op;
    bus.publish_ota("a", "g", op);
    GateCommand gc;
    gc.set_gate_id("g");
    gc.set_kind(CommandKind::COMMAND_KIND_OPEN_GATE);
    bus.publish_command("a", gc);
    CommandAck ack;
    ack.set_command_id("cmd-1");
    bus.publish_ack("a", "g", ack);

    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 6);
}

TEST_CASE("EventBroadcaster: GateCommand and CommandAck always pass when kind filter active",
          "[dash][filter]") {
    EventBroadcaster bus;
    DashboardSubscription filter;
    // include_decisions=true implies "kind filter active" — but Command/Ack
    // have no flag of their own, so they remain visible.
    filter.set_include_decisions(true);
    auto sub = bus.subscribe(filter);

    bus.publish_telemetry("a", make_telemetry("g"));  // dropped
    GateCommand gc;
    gc.set_gate_id("g");
    bus.publish_command("a", gc);
    CommandAck ack;
    ack.set_command_id("cmd-1");
    bus.publish_ack("a", "g", ack);

    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 2);
    REQUIRE(evs[0].payload_case() == DashboardEvent::kCommand);
    REQUIRE(evs[1].payload_case() == DashboardEvent::kAck);
}

TEST_CASE("EventBroadcaster: replay since_event_id streams missed events", "[dash][replay]") {
    EventBroadcaster bus;

    // Publish 5 events before any subscriber connects.
    for (int i = 0; i < 5; ++i) {
        bus.publish_telemetry("a", make_telemetry("g1"));
    }
    REQUIRE(bus.last_event_id() == 5);

    // Subscribe with since=2 — expect events 3, 4, 5.
    DashboardSubscription filter;
    filter.set_since_event_id(2);
    auto sub = bus.subscribe(filter);
    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 3);
    REQUIRE(evs[0].event_id() == 3);
    REQUIRE(evs[1].event_id() == 4);
    REQUIRE(evs[2].event_id() == 5);
}

TEST_CASE("EventBroadcaster: ring buffer evicts oldest events", "[dash][replay][ring]") {
    EventBroadcaster bus{/*ring_capacity=*/4, /*per_sub_capacity=*/16};

    for (int i = 0; i < 10; ++i) {
        bus.publish_telemetry("a", make_telemetry("g1"));
    }
    REQUIRE(bus.last_event_id() == 10);

    // Late subscriber asking for since=0 only sees the last 4 events
    // (7, 8, 9, 10) — the ring forgot the rest.
    DashboardSubscription filter;
    auto sub = bus.subscribe(filter);
    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 4);
    REQUIRE(evs.front().event_id() == 7);
    REQUIRE(evs.back().event_id() == 10);
}

TEST_CASE("EventBroadcaster: per-subscriber queue drops oldest under back-pressure",
          "[dash][backpressure]") {
    EventBroadcaster bus{/*ring=*/4096, /*per_sub=*/3};

    auto sub = bus.subscribe(DashboardSubscription{});
    REQUIRE(sub->dropped_events() == 0);

    for (int i = 0; i < 5; ++i) {
        bus.publish_telemetry("a", make_telemetry("g"));
    }
    // The slow subscriber's 3-deep queue dropped the first 2.
    REQUIRE(sub->dropped_events() == 2);
    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 3);
    REQUIRE(evs[0].event_id() == 3);  // ids 1-2 were dropped from the per-sub queue
    REQUIRE(evs[2].event_id() == 5);
}

TEST_CASE("EventBroadcaster: stop() wakes blocked subscribers", "[dash][stop]") {
    EventBroadcaster bus;
    auto sub = bus.subscribe(DashboardSubscription{});

    std::optional<DashboardEvent> result;
    std::thread waiter([&] { result = sub->next(std::chrono::seconds{5}); });

    // Give the thread a moment to enter the wait, then stop.
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    bus.stop();
    waiter.join();

    REQUIRE_FALSE(result.has_value());
    REQUIRE(bus.stopped());
    // Re-publishing after stop is a no-op (returns 0).
    REQUIRE(bus.publish_telemetry("a", make_telemetry("g")) == 0);
}

TEST_CASE("EventBroadcaster: subscribe() after stop returns nullptr", "[dash][stop]") {
    EventBroadcaster bus;
    bus.stop();
    REQUIRE(bus.subscribe(DashboardSubscription{}) == nullptr);
}

TEST_CASE("EventBroadcaster: closed subscriber's slot is reaped on next publish",
          "[dash][lifecycle]") {
    EventBroadcaster bus;
    {
        auto sub = bus.subscribe(DashboardSubscription{});
        REQUIRE(bus.subscriber_count() == 1);
    }
    // The subscriber's shared_ptr is gone; subscriber_count() prunes
    // expired weak_ptrs on the next call.
    bus.publish_telemetry("a", make_telemetry("g"));
    REQUIRE(bus.subscriber_count() == 0);
}

TEST_CASE("EventBroadcaster: multiple subscribers each get their own copy", "[dash][multi-sub]") {
    EventBroadcaster bus;
    auto a = bus.subscribe(DashboardSubscription{});
    auto b = bus.subscribe(DashboardSubscription{});

    bus.publish_telemetry("site", make_telemetry("g"));
    bus.publish_telemetry("site", make_telemetry("g"));

    REQUIRE(a->drain_now().size() == 2);
    REQUIRE(b->drain_now().size() == 2);
}

TEST_CASE("filter_matches: site/gate/kind decision table", "[dash][filter][unit]") {
    DashboardSubscription empty;
    DashboardEvent ev;
    *ev.mutable_decision() = make_decision("g1", AuthVerdict::AUTH_VERDICT_AUTHORIZED);

    REQUIRE(filter_matches(empty, "site-a", "g1", ev));

    DashboardSubscription site_only;
    site_only.set_site_id("site-b");
    REQUIRE_FALSE(filter_matches(site_only, "site-a", "g1", ev));
    REQUIRE(filter_matches(site_only, "site-b", "g1", ev));

    DashboardSubscription gate_only;
    gate_only.add_gate_ids("g2");
    REQUIRE_FALSE(filter_matches(gate_only, "site-a", "g1", ev));
    REQUIRE(filter_matches(gate_only, "site-a", "g2", ev));

    DashboardSubscription only_telemetry;
    only_telemetry.set_include_telemetry(true);
    REQUIRE_FALSE(filter_matches(only_telemetry, "site-a", "g1", ev));  // ev is decision
}
