// gate_arbiter_test.cpp — spec section 10 rules and races.

#include "rpc/gate_arbiter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using gate::rpc::GateArbiter;
using gate::v1::GATE_SIDE_ENTRY;
using gate::v1::GATE_SIDE_EXIT;
using gate::v1::OpenReply;

namespace {

struct Rig {
    std::vector<std::string> grants;
    std::vector<GateArbiter::Audit> audits;
    bool valid = true;
    GateArbiter a;
    explicit Rig(GateArbiter::Config c = {})
        : a(
              c, [this](const GateArbiter::Request& r) { grants.push_back(r.request_id); },
              [this](const GateArbiter::Audit& x) { audits.push_back(x); },
              [this](const GateArbiter::Request&) { return valid; }) {}
    OpenReply entry(const std::string& id, std::uint64_t t, const std::string& key = "") {
        return a.submit({id, GATE_SIDE_ENTRY, gate::v1::OPEN_SOURCE_PLATE, key}, t);
    }
    OpenReply exit(const std::string& id, std::uint64_t t) {
        return a.submit({id, GATE_SIDE_EXIT, gate::v1::OPEN_SOURCE_RTE, ""}, t);
    }
};

}  // namespace

TEST_CASE("waiting entry and exit: exit goes first, both logged", "[arbiter]") {
    Rig r;
    r.exit("x0", 0);
    CHECK(r.entry("e", 100).status() == OpenReply::QUEUED);
    r.exit("x", 300);
    r.a.vehicle_cleared(5'000);
    r.a.vehicle_cleared(6'000);
    REQUIRE(r.grants == std::vector<std::string>{"x0", "x", "e"});
    CHECK(r.audits[2].preempted_by == "x");
    CHECK(r.audits[2].queued_ms == 5'900);
}

TEST_CASE("lone entry is granted at once", "[arbiter]") {
    Rig r;
    CHECK(r.entry("e", 0).status() == OpenReply::GRANTED);
}

TEST_CASE("served ids stay bounded, newest remembered", "[arbiter]") {
    Rig r;
    for (std::size_t i = 0; i <= GateArbiter::kMaxServed; ++i) {
        r.a.mark_served_offline("s" + std::to_string(i), 0);
    }
    CHECK(r.exit("s" + std::to_string(GateArbiter::kMaxServed), 1).status() ==
          OpenReply::ALREADY_SERVED);
    CHECK(r.exit("s0", 1).status() == OpenReply::GRANTED);
}

TEST_CASE("occupied throat holds grants until clear", "[arbiter]") {
    Rig r;
    r.a.set_occupied(true, 0);
    CHECK(r.exit("x", 0).status() == OpenReply::QUEUED);
    r.a.set_occupied(false, 10);
    CHECK(r.grants.size() == 1);
}

TEST_CASE("unused grant times out and the next goes", "[arbiter]") {
    Rig r;
    r.exit("x", 0);
    r.entry("e", 1'000);
    r.a.tick(30'000);
    CHECK(r.grants.size() == 2);
    CHECK(r.audits[1].outcome == "abandoned");
}

TEST_CASE("entry expires by TTL", "[arbiter]") {
    Rig r;
    r.exit("x", 0);
    r.entry("e", 0);
    r.a.tick(120'000);
    CHECK(r.a.waiting() == 0);
    CHECK(r.grants.size() == 1);
}

TEST_CASE("no starvation after three exits", "[arbiter]") {
    Rig r;
    r.exit("x0", 0);
    r.entry("e", 1'000);
    r.exit("x1", 1'001);
    r.exit("x2", 1'002);
    r.exit("x3", 1'003);
    for (std::uint64_t t = 2'000; t < 7'000; t += 1'000) {
        r.a.vehicle_cleared(t);
    }
    CHECK(r.grants == std::vector<std::string>{"x0", "x1", "x2", "e", "x3"});
}

TEST_CASE("queued entry is revalidated at grant", "[arbiter]") {
    Rig r;
    r.a.set_occupied(true, 0);
    r.entry("e", 0);
    r.valid = false;
    r.a.set_occupied(false, 600);
    CHECK(r.grants.empty());
    CHECK(r.audits[0].outcome == "refused");
}

TEST_CASE("repeat plate read refreshes, not duplicates", "[arbiter]") {
    Rig r;
    r.a.set_occupied(true, 0);
    r.entry("a", 0, "KDA");
    CHECK(r.entry("b", 100, "KDA").status() == OpenReply::QUEUED);
    CHECK(r.a.waiting() == 1);
}

TEST_CASE("offline-served request cannot be granted late", "[arbiter]") {
    Rig r;
    r.a.set_occupied(true, 0);
    r.exit("x", 0);
    r.a.mark_served_offline("x", 10);
    r.a.set_occupied(false, 20);
    CHECK(r.grants.empty());
    CHECK(r.exit("x", 30).status() == OpenReply::ALREADY_SERVED);
}

TEST_CASE("two-lane sides do not wait for each other", "[arbiter]") {
    GateArbiter::Config c;
    c.lane_mode = gate::v1::LANE_MODE_TWO_LANE;
    Rig r(c);
    CHECK(r.exit("x", 0).status() == OpenReply::GRANTED);
    CHECK(r.entry("e", 1).status() == OpenReply::GRANTED);
}
