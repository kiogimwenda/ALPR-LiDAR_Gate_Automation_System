// event_cache_test.cpp — ring, per-gate state, fan-out, resume cursor.

#include "dash_api/event_cache.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace dash = gate::dash_api;

namespace {

gate::v1::DashboardEvent telemetry_event(std::uint64_t id, const std::string& gate,
                                         gate::v1::GateState state) {
    gate::v1::DashboardEvent ev;
    ev.set_event_id(id);
    ev.mutable_event_ts()->set_seconds(1'751'700'000 + static_cast<std::int64_t>(id));
    ev.mutable_telemetry()->set_gate_id(gate);
    ev.mutable_telemetry()->set_gate_state(state);
    return ev;
}

}  // namespace

TEST_CASE("sinks receive each frame exactly once, serialised once") {
    dash::EventCache cache;
    std::vector<std::string> a;
    std::vector<std::string> b;
    const auto ida = cache.add_sink([&](const std::string& f) { a.push_back(f); });
    cache.add_sink([&](const std::string& f) { b.push_back(f); });

    cache.ingest(telemetry_event(1, "gate-01", gate::v1::GATE_STATE_CLOSED));
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(a[0] == b[0]);
    CHECK(a[0].find("\"type\":\"telemetry\"") != std::string::npos);

    cache.remove_sink(ida);
    cache.ingest(telemetry_event(2, "gate-01", gate::v1::GATE_STATE_OPENING));
    CHECK(a.size() == 1);  // detached
    CHECK(b.size() == 2);
    CHECK(cache.sink_count() == 1);
}

TEST_CASE("ring keeps the newest N frames, oldest first") {
    dash::EventCache cache(/*ring_capacity=*/3);
    for (std::uint64_t i = 1; i <= 5; ++i) {
        cache.ingest(telemetry_event(i, "gate-01", gate::v1::GATE_STATE_CLOSED));
    }
    const auto frames = cache.recent_frames();
    REQUIRE(frames.size() == 3);
    CHECK(frames.front().find("\"eventId\":3") != std::string::npos);
    CHECK(frames.back().find("\"eventId\":5") != std::string::npos);
}

TEST_CASE("status snapshot carries the latest telemetry per gate") {
    dash::EventCache cache;
    cache.ingest(telemetry_event(1, "gate-01", gate::v1::GATE_STATE_CLOSED));
    cache.ingest(telemetry_event(2, "gate-02", gate::v1::GATE_STATE_OPEN));
    cache.ingest(telemetry_event(3, "gate-01", gate::v1::GATE_STATE_OPENING));

    const auto v = cache.status_snapshot();
    CHECK(v["lastEventId"].asUInt64() == 3);
    REQUIRE(v["gates"].isMember("gate-01"));
    REQUIRE(v["gates"].isMember("gate-02"));
    CHECK(v["gates"]["gate-01"]["telemetry"]["state"].asString() == "OPENING");
    CHECK(v["gates"]["gate-02"]["telemetry"]["state"].asString() == "OPEN");
}

TEST_CASE("resume cursor is the highest event id seen, not the last ingested") {
    dash::EventCache cache;
    cache.ingest(telemetry_event(7, "gate-01", gate::v1::GATE_STATE_CLOSED));
    cache.ingest(telemetry_event(5, "gate-01", gate::v1::GATE_STATE_CLOSED));  // replayed dup
    CHECK(cache.last_event_id() == 7);
}
