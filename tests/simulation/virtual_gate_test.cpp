// virtual_gate_test.cpp — gate types, locks and latches in simulated time.

#include "gate_sim/virtual_gate.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "gate_service.pb.h"

namespace sm = gate::state_machine;
using gate::sim::VirtualGate;
using sm::State;

namespace {

VirtualGate::Config cfg(sm::GateType type) {
    VirtualGate::Config c;
    c.gate_type = type;
    c.lock_layout = type == sm::GateType::DualLeaf ? sm::LockLayout::Five : sm::LockLayout::Four;
    c.profile = sm::ActuatorProfile::Operator;
    c.travel_ms = 4'000;
    c.leaf_delay_ms = 1'000;
    c.offline_close_ms = 2'000;
    return c;
}

void run(VirtualGate& g, std::uint32_t ms) {
    for (std::uint32_t t = 0; t < ms; t += 20) {
        g.advance(20);
    }
}

}  // namespace

TEST_CASE("wire state follows spec 8.5", "[wire]") {
    using namespace gate::v1;
    CHECK(sm::wire_state(State::Initializing) == GATE_STATE_UNKNOWN);
    CHECK(sm::wire_state(State::Closed) == GATE_STATE_CLOSED);
    CHECK(sm::wire_state(State::Opening) == GATE_STATE_OPENING);
    CHECK(sm::wire_state(State::Open) == GATE_STATE_OPEN);
    CHECK(sm::wire_state(State::Closing) == GATE_STATE_CLOSING);
    CHECK(sm::wire_state(State::Faulted) == GATE_STATE_FAULT);
    CHECK(sm::wire_state(State::Lockdown) == GATE_STATE_LOCKDOWN);
    CHECK(sm::wire_state(State::Unlocking) == GATE_STATE_UNLOCKING);
    CHECK(sm::wire_state(State::Locking) == GATE_STATE_LOCKING);
    CHECK(sm::wire_state(State::ClosedInsecure) == GATE_STATE_CLOSED_INSECURE);
    CHECK(sm::wire_state(State::Verifying) == GATE_STATE_VERIFYING);
    CHECK(sm::wire_state(State::Calibrating) == GATE_STATE_CALIBRATING);
    CHECK(sm::wire_state(State::PreClose) == GATE_STATE_PRE_CLOSE);
    CHECK(sm::wire_state(State::StoppedOpen) == GATE_STATE_STOPPED);
    CHECK(sm::wire_state(State::StoppedClose) == GATE_STATE_STOPPED);
}

TEST_CASE("tracker verdicts for lock states", "[tracker]") {
    gate::rpc::CommandTracker t;
    const auto arm = [&](State terminal) { t.arm("c", terminal, 1'000); };
    SECTION("Lockdown completes a close") {
        arm(State::Closed);
        const auto v = t.on_gate_event(State::Lockdown, sm::Reason::None, true);
        CHECK((v.fire && v.success));
    }
    SECTION("ClosedInsecure fails a close") {
        arm(State::Closed);
        const auto v = t.on_gate_event(State::ClosedInsecure, sm::Reason::LockIncomplete, true);
        CHECK((v.fire && !v.success));
    }
    SECTION("operator reversal fails a close") {
        arm(State::Closed);
        const auto v = t.on_gate_event(State::Opening, sm::Reason::SafetyBeamObstacle, true);
        CHECK((v.fire && !v.success));
    }
    SECTION("refusals fail at once") {
        const auto r = GENERATE(sm::Reason::Lockdown, sm::Reason::BatteryReserve,
                                sm::Reason::KeyOverride, sm::Reason::Busy);
        arm(State::Open);
        const auto v = t.on_gate_event(State::Closed, r, false);
        CHECK((v.fire && !v.success));
    }
    SECTION("intermediate lock states stay pending") {
        arm(State::Open);
        CHECK_FALSE(t.on_gate_event(State::Unlocking, sm::Reason::None, true).fire);
    }
}

TEST_CASE("each gate type runs a full locked cycle", "[virtual_gate]") {
    const auto type =
        GENERATE(sm::GateType::Sliding, sm::GateType::SingleLeaf, sm::GateType::DualLeaf);
    VirtualGate g(cfg(type));
    g.boot_closed();
    REQUIRE(g.state() == State::Closed);
    REQUIRE(g.bolts_extended());
    REQUIRE(g.maglocks_bonded());

    g.issue("open", VirtualGate::Command::kOpen);
    run(g, 8'000);
    CHECK(g.state() == State::Open);
    CHECK(g.limit_open());
    CHECK_FALSE(g.bolts_extended());
    CHECK_FALSE(g.maglocks_bonded());

    g.issue("close", VirtualGate::Command::kClose);
    run(g, 8'000);
    CHECK(g.state() == State::Closed);
    CHECK(g.limit_closed());
    CHECK(g.bolts_extended());
    CHECK(g.maglocks_bonded());
    CHECK_FALSE(g.permit());

    const auto acks = g.drain_acks();
    REQUIRE(acks.size() == 2);
    CHECK(acks[0].success);
    CHECK(acks[1].success);
}

TEST_CASE("dual leaf lags leaf B by the operator's delay", "[virtual_gate]") {
    VirtualGate g(cfg(sm::GateType::DualLeaf));
    g.boot_closed();
    g.local_open();
    run(g, 1'300);
    CHECK(g.position_permille_a() > g.position_permille_b());
    CHECK(g.position_permille_b() == 0.0);
}

TEST_CASE("the interlock holds against a firmware bug", "[virtual_gate]") {
    VirtualGate g(cfg(sm::GateType::Sliding));
    g.boot_closed();
    g.force_motor_open();
    run(g, 2'000);
    CHECK(g.position_permille_a() == 0.0);
}

TEST_CASE("a jammed bolt faults with no motion", "[virtual_gate]") {
    VirtualGate g(cfg(sm::GateType::Sliding));
    g.boot_closed();
    g.jam_bolt(1, true);
    g.local_open();
    run(g, 6'000);
    CHECK(g.state() == State::Faulted);
    CHECK(g.reason() == sm::Reason::LockJam);
    CHECK(g.position_permille_a() == 0.0);

    g.jam_bolt(1, false);
    g.rte();  // one fresh retract test
    run(g, 8'000);
    CHECK(g.state() == State::Open);
}

TEST_CASE("a maglock that will not release faults with no motion", "[virtual_gate]") {
    VirtualGate g(cfg(sm::GateType::DualLeaf));
    g.boot_closed();
    g.stick_maglock(2, true);
    g.local_open();
    run(g, 3'000);
    CHECK(g.state() == State::Faulted);
    CHECK(g.reason() == sm::Reason::LockJam);
    CHECK(g.position_permille_a() == 0.0);
}

TEST_CASE("lockdown persists after an RTE cycle and a reboot", "[virtual_gate]") {
    VirtualGate g(cfg(sm::GateType::Sliding));
    g.boot_closed();
    REQUIRE(g.latch_close());
    CHECK(g.state() == State::Lockdown);

    g.local_open();
    run(g, 200);
    CHECK(g.state() == State::Lockdown);

    g.rte();
    run(g, 5'000);
    CHECK(g.state() == State::Open);
    run(g, 8'000);  // offline close, then the lock sequence
    CHECK(g.state() == State::Lockdown);

    g.reboot();
    run(g, 4'000);
    CHECK(g.state() == State::Lockdown);
    CHECK(g.lockdown_latched());

    g.release_latch();
    CHECK(g.state() == State::Closed);
}

TEST_CASE("operator beam reversal is followed", "[virtual_gate]") {
    VirtualGate g(cfg(sm::GateType::Sliding));
    g.boot_closed();
    g.local_open();
    run(g, 6'000);
    REQUIRE(g.state() == State::Open);
    g.local_close();
    run(g, 2'000);
    REQUIRE(g.state() == State::Closing);
    g.trip_beam();
    CHECK(g.state() == State::Opening);
    run(g, 4'000);
    CHECK(g.state() == State::Open);
}

TEST_CASE("key override cuts the maglocks and relocks on release", "[virtual_gate]") {
    VirtualGate g(cfg(sm::GateType::Sliding));
    g.boot_closed();
    g.set_key_override(true);
    run(g, 500);
    CHECK(g.state() == State::ClosedInsecure);
    CHECK_FALSE(g.maglocks_bonded());
    g.set_key_override(false);
    run(g, 3'000);
    CHECK(g.state() == State::Closed);
}

TEST_CASE("wrong gate type stops, closes and locks", "[virtual_gate]") {
    VirtualGate g(cfg(sm::GateType::Sliding));
    g.boot_closed();
    g.begin_verify(sm::GateType::DualLeaf);
    run(g, 12'000);
    CHECK(g.state() == State::Closed);
    CHECK(g.reason() == sm::Reason::None);
    CHECK(g.limit_closed());
}

TEST_CASE("latch open holds past auto-close", "[virtual_gate]") {
    auto c = cfg(sm::GateType::Sliding);
    c.auto_close_ms = 1'000;
    VirtualGate g(c);
    g.boot_closed();
    REQUIRE(g.latch_open());
    run(g, 10'000);
    CHECK(g.state() == State::Open);
    g.release_latch();
    run(g, 8'000);
    CHECK(g.state() == State::Closed);
}

TEST_CASE("bolts rest during a long open and re-prove before close", "[virtual_gate]") {
    auto c = cfg(sm::GateType::Sliding);
    c.bolt_hold_open_ms = 1'000;
    VirtualGate g(c);
    g.boot_closed();
    g.local_open();
    run(g, 8'000);
    CHECK(g.state() == State::Open);
    CHECK(g.bolts_extended());
    CHECK_FALSE(g.permit());
    g.local_close();
    run(g, 7'000);
    CHECK(g.state() == State::Closed);
}

TEST_CASE("legacy gate keeps its old cycle with no locks", "[virtual_gate][legacy]") {
    VirtualGate::Config c;
    c.travel_ms = 1'000;
    VirtualGate g(c);
    g.boot_closed();
    g.local_open();
    CHECK(g.state() == State::Opening);
    run(g, 1'200);
    CHECK(g.state() == State::Open);
    CHECK_FALSE(g.latch_close());
}
