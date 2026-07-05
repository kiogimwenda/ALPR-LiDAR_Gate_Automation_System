// command_tracker_test.cpp — the four completion rules for pending
// GateCommands, plus supersession.

#include "gate_rpc/command_tracker.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

namespace rpc = gate::rpc;
namespace sm = gate::state_machine;

TEST_CASE("unarmed tracker stays silent") {
    rpc::CommandTracker t;
    CHECK_FALSE(t.armed());
    CHECK_FALSE(t.on_gate_event(sm::State::Open, sm::Reason::None, true).fire);
    CHECK_FALSE(t.on_tick(999'999).fire);
}

TEST_CASE("open command completes when the gate reaches Open") {
    rpc::CommandTracker t;
    CHECK_FALSE(t.arm("cmd-open-1", sm::State::Open, 40'000).fire);
    REQUIRE(t.armed());

    // Intermediate transition: still pending.
    CHECK_FALSE(t.on_gate_event(sm::State::Opening, sm::Reason::None, true).fire);

    const auto v = t.on_gate_event(sm::State::Open, sm::Reason::None, true);
    REQUIRE(v.fire);
    CHECK(v.success);
    CHECK(std::strcmp(v.command_id, "cmd-open-1") == 0);
    CHECK_FALSE(t.armed());

    // Fires at most once.
    CHECK_FALSE(t.on_gate_event(sm::State::Open, sm::Reason::None, true).fire);
}

TEST_CASE("fault during execution fails the command with the reason") {
    rpc::CommandTracker t;
    t.arm("cmd-open-2", sm::State::Open, 40'000);
    const auto v = t.on_gate_event(sm::State::Faulted, sm::Reason::MotorTimeout, true);
    REQUIRE(v.fire);
    CHECK_FALSE(v.success);
    CHECK(std::strcmp(v.error, "MotorTimeout") == 0);
    CHECK_FALSE(t.armed());
}

TEST_CASE("beam-latch rejection fails a close without a transition") {
    rpc::CommandTracker t;
    t.arm("cmd-close-1", sm::State::Closed, 40'000);
    // The state machine refuses: stays Open, reason set, no transition.
    const auto v = t.on_gate_event(sm::State::Open, sm::Reason::SafetyBeamObstacle, false);
    REQUIRE(v.fire);
    CHECK_FALSE(v.success);
    CHECK(std::strcmp(v.command_id, "cmd-close-1") == 0);
}

TEST_CASE("deadline expiry fails a stuck command, wrap-safe") {
    rpc::CommandTracker t;
    t.arm("cmd-close-2", sm::State::Closed, 10'000);
    CHECK_FALSE(t.on_tick(9'999).fire);
    const auto v = t.on_tick(10'000);
    REQUIRE(v.fire);
    CHECK_FALSE(v.success);

    // Deadline near uint32 wraparound still compares correctly.
    rpc::CommandTracker t2;
    t2.arm("cmd-wrap", sm::State::Open, 0xFFFFFFF0U);
    CHECK_FALSE(t2.on_tick(0xFFFFFFE0U).fire);  // 16 ms before deadline
    CHECK(t2.on_tick(0x00000010U).fire);        // 32 ms after, post-wrap
}

TEST_CASE("arming over a live command supersedes it") {
    rpc::CommandTracker t;
    t.arm("cmd-a", sm::State::Open, 40'000);
    const auto old = t.arm("cmd-b", sm::State::Closed, 50'000);
    REQUIRE(old.fire);
    CHECK_FALSE(old.success);
    CHECK(std::strcmp(old.command_id, "cmd-a") == 0);

    // The new command tracks normally.
    const auto v = t.on_gate_event(sm::State::Closed, sm::Reason::None, true);
    REQUIRE(v.fire);
    CHECK(v.success);
    CHECK(std::strcmp(v.command_id, "cmd-b") == 0);
}

TEST_CASE("stop of an unrelated motion leaves the command pending") {
    rpc::CommandTracker t;
    t.arm("cmd-open-3", sm::State::Open, 40'000);
    CHECK_FALSE(t.on_gate_event(sm::State::StoppedOpen, sm::Reason::OperatorStop, true).fire);
    CHECK(t.armed());  // deadline remains the backstop
}
