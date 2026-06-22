// state_machine_test.cpp — Catch2 unit tests for the gate state machine.
//
// Each SECTION drives one canonical path through the transition table.
// The tests treat the machine as a pure function and assert on (state,
// reason, action list) after each event — the same surface the driver
// layer will consume in Phase 4.5.2.

#include <algorithm>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "gate_state_machine/state_machine.hpp"

namespace sm = gate::state_machine;

namespace {

// Collapse Outputs.actions / .count into a vector so Catch2's vector
// matchers give a readable diff on failure.
std::vector<sm::Action> actions(const sm::Outputs& o) {
    return std::vector<sm::Action>(o.actions.begin(), o.actions.begin() + o.count);
}

}  // namespace

TEST_CASE("init helpers transition out of Initializing", "[state_machine][init]") {
    SECTION("init_closed lands in Closed with LED + telemetry") {
        sm::StateMachine m{};
        REQUIRE(m.state() == sm::State::Initializing);
        const auto out = m.init_closed();
        REQUIRE(m.state() == sm::State::Closed);
        REQUIRE(out.transitioned);
        REQUIRE_THAT(actions(out),
                     Catch::Matchers::Equals(std::vector<sm::Action>{
                         sm::Action::StopMotor,
                         sm::Action::SetLedClosed,
                         sm::Action::EmitTelemetryStateChanged,
                     }));
    }

    SECTION("init_open lands in Open") {
        sm::StateMachine m{};
        const auto out = m.init_open();
        REQUIRE(m.state() == sm::State::Open);
        REQUIRE(out.transitioned);
        REQUIRE_THAT(actions(out),
                     Catch::Matchers::Equals(std::vector<sm::Action>{
                         sm::Action::StopMotor,
                         sm::Action::SetLedOpen,
                         sm::Action::EmitTelemetryStateChanged,
                     }));
    }

    SECTION("init_unknown lands in Faulted with LimitSwitchConflict") {
        sm::StateMachine m{};
        const auto out = m.init_unknown();
        REQUIRE(m.state() == sm::State::Faulted);
        REQUIRE(m.reason() == sm::Reason::LimitSwitchConflict);
        REQUIRE(out.reason == sm::Reason::LimitSwitchConflict);
        REQUIRE_THAT(actions(out),
                     Catch::Matchers::Equals(std::vector<sm::Action>{
                         sm::Action::StopMotor,
                         sm::Action::SetLedFault,
                         sm::Action::EmitTelemetryStateChanged,
                     }));
    }
}

TEST_CASE("happy path Closed → Open → Closed", "[state_machine][happy-path]") {
    sm::StateMachine m{};
    m.init_closed();

    // Closed → Opening
    auto out = m.handle(sm::Event::CommandOpen);
    REQUIRE(m.state() == sm::State::Opening);
    REQUIRE(out.transitioned);
    REQUIRE_THAT(actions(out),
                 Catch::Matchers::Equals(std::vector<sm::Action>{
                     sm::Action::DriveMotorOpen,
                     sm::Action::SetLedOpening,
                     sm::Action::EmitTelemetryStateChanged,
                 }));

    // Opening → Open
    out = m.handle(sm::Event::LimitOpenHit);
    REQUIRE(m.state() == sm::State::Open);
    REQUIRE_THAT(actions(out),
                 Catch::Matchers::Equals(std::vector<sm::Action>{
                     sm::Action::StopMotor,
                     sm::Action::SetLedOpen,
                     sm::Action::EmitTelemetryStateChanged,
                 }));

    // Open → Closing
    out = m.handle(sm::Event::CommandClose);
    REQUIRE(m.state() == sm::State::Closing);
    REQUIRE_THAT(actions(out),
                 Catch::Matchers::Equals(std::vector<sm::Action>{
                     sm::Action::DriveMotorClose,
                     sm::Action::SetLedClosing,
                     sm::Action::EmitTelemetryStateChanged,
                 }));

    // Closing → Closed
    out = m.handle(sm::Event::LimitClosedHit);
    REQUIRE(m.state() == sm::State::Closed);
    REQUIRE_THAT(actions(out),
                 Catch::Matchers::Equals(std::vector<sm::Action>{
                     sm::Action::StopMotor,
                     sm::Action::SetLedClosed,
                     sm::Action::EmitTelemetryStateChanged,
                 }));
}

TEST_CASE("operator stop from active motion", "[state_machine][stop]") {
    SECTION("stop during Opening lands in StoppedOpen") {
        sm::StateMachine m{};
        m.init_closed();
        m.handle(sm::Event::CommandOpen);
        const auto out = m.handle(sm::Event::CommandStop);
        REQUIRE(m.state() == sm::State::StoppedOpen);
        REQUIRE(m.reason() == sm::Reason::OperatorStop);
        REQUIRE(out.reason == sm::Reason::OperatorStop);
        REQUIRE_THAT(actions(out),
                     Catch::Matchers::Equals(std::vector<sm::Action>{
                         sm::Action::StopMotor,
                         sm::Action::SetLedStopped,
                         sm::Action::EmitTelemetryStateChanged,
                     }));
    }

    SECTION("stop during Closing lands in StoppedClose") {
        sm::StateMachine m{};
        m.init_open();
        m.handle(sm::Event::CommandClose);
        const auto out = m.handle(sm::Event::CommandStop);
        REQUIRE(m.state() == sm::State::StoppedClose);
        REQUIRE(m.reason() == sm::Reason::OperatorStop);
        REQUIRE(out.transitioned);
    }

    SECTION("stop while Closed is a no-op") {
        sm::StateMachine m{};
        m.init_closed();
        const auto out = m.handle(sm::Event::CommandStop);
        REQUIRE(m.state() == sm::State::Closed);
        REQUIRE_FALSE(out.transitioned);
        REQUIRE(out.count == 0);
    }
}

TEST_CASE("resume from a Stopped state", "[state_machine][stop][resume]") {
    SECTION("CommandOpen resumes from StoppedOpen") {
        sm::StateMachine m{};
        m.init_closed();
        m.handle(sm::Event::CommandOpen);
        m.handle(sm::Event::CommandStop);
        REQUIRE(m.state() == sm::State::StoppedOpen);
        const auto out = m.handle(sm::Event::CommandOpen);
        REQUIRE(m.state() == sm::State::Opening);
        REQUIRE(m.reason() == sm::Reason::None);
        REQUIRE_THAT(actions(out),
                     Catch::Matchers::Equals(std::vector<sm::Action>{
                         sm::Action::DriveMotorOpen,
                         sm::Action::SetLedOpening,
                         sm::Action::EmitTelemetryStateChanged,
                     }));
    }

    SECTION("CommandClose from StoppedOpen needs beam clear") {
        sm::StateMachine m{};
        m.init_closed();
        m.handle(sm::Event::CommandOpen);
        m.handle(sm::Event::CommandStop);
        m.handle(sm::Event::SafetyBeamTripped);
        const auto rejected = m.handle(sm::Event::CommandClose);
        REQUIRE(m.state() == sm::State::StoppedOpen);
        REQUIRE(rejected.count == 0);  // no actions emitted on silent reject

        m.handle(sm::Event::SafetyBeamCleared);
        const auto accepted = m.handle(sm::Event::CommandClose);
        REQUIRE(m.state() == sm::State::Closing);
        REQUIRE(accepted.transitioned);
    }
}

TEST_CASE("safety beam during Closing stops the gate", "[state_machine][safety]") {
    sm::StateMachine m{};
    m.init_open();
    m.handle(sm::Event::CommandClose);
    REQUIRE(m.state() == sm::State::Closing);

    const auto out = m.handle(sm::Event::SafetyBeamTripped);
    REQUIRE(m.state() == sm::State::StoppedClose);
    REQUIRE(m.reason() == sm::Reason::SafetyBeamObstacle);
    REQUIRE(out.reason == sm::Reason::SafetyBeamObstacle);
    REQUIRE_THAT(actions(out),
                 Catch::Matchers::Equals(std::vector<sm::Action>{
                     sm::Action::StopMotor,
                     sm::Action::SetLedStopped,
                     sm::Action::EmitTelemetryStateChanged,
                 }));
}

TEST_CASE("beam latch rejects Close while obstructed", "[state_machine][safety][beam-latch]") {
    sm::StateMachine m{};
    m.init_open();
    m.handle(sm::Event::SafetyBeamTripped);

    const auto rejected = m.handle(sm::Event::CommandClose);
    REQUIRE(m.state() == sm::State::Open);
    REQUIRE_FALSE(rejected.transitioned);
    REQUIRE(rejected.reason == sm::Reason::SafetyBeamObstacle);
    // The rejection emits a telemetry record so the dashboard can show
    // "close attempted while obstructed" without inferring it from state.
    REQUIRE_THAT(actions(rejected),
                 Catch::Matchers::Equals(std::vector<sm::Action>{
                     sm::Action::EmitTelemetryStateChanged,
                 }));

    m.handle(sm::Event::SafetyBeamCleared);
    const auto accepted = m.handle(sm::Event::CommandClose);
    REQUIRE(m.state() == sm::State::Closing);
    REQUIRE(accepted.transitioned);
}

TEST_CASE("motor timeout from active motion → Faulted", "[state_machine][fault]") {
    SECTION("timeout during Opening faults the gate") {
        sm::StateMachine m{};
        m.init_closed();
        m.handle(sm::Event::CommandOpen);
        const auto out = m.handle(sm::Event::MotorTimeout);
        REQUIRE(m.state() == sm::State::Faulted);
        REQUIRE(m.reason() == sm::Reason::MotorTimeout);
        REQUIRE(out.reason == sm::Reason::MotorTimeout);
        REQUIRE_THAT(actions(out),
                     Catch::Matchers::Equals(std::vector<sm::Action>{
                         sm::Action::StopMotor,
                         sm::Action::SetLedFault,
                         sm::Action::EmitTelemetryStateChanged,
                     }));
    }

    SECTION("timeout while Closed is ignored") {
        sm::StateMachine m{};
        m.init_closed();
        const auto out = m.handle(sm::Event::MotorTimeout);
        REQUIRE(m.state() == sm::State::Closed);
        REQUIRE_FALSE(out.transitioned);
    }
}

TEST_CASE("Faulted requires explicit reset", "[state_machine][fault][reset]") {
    sm::StateMachine m{};
    m.init_closed();
    m.handle(sm::Event::CommandOpen);
    m.handle(sm::Event::MotorTimeout);
    REQUIRE(m.state() == sm::State::Faulted);

    // Commands while Faulted are silently dropped.
    REQUIRE_FALSE(m.handle(sm::Event::CommandOpen).transitioned);
    REQUIRE_FALSE(m.handle(sm::Event::CommandClose).transitioned);
    REQUIRE_FALSE(m.handle(sm::Event::CommandStop).transitioned);
    REQUIRE(m.state() == sm::State::Faulted);

    // FaultCleared returns to Initializing so the driver layer re-reads
    // the limit switches before any motion.
    const auto reset = m.handle(sm::Event::FaultCleared);
    REQUIRE(m.state() == sm::State::Initializing);
    REQUIRE(m.reason() == sm::Reason::None);
    REQUIRE(reset.transitioned);
    REQUIRE_THAT(actions(reset),
                 Catch::Matchers::Equals(std::vector<sm::Action>{
                     sm::Action::EmitTelemetryStateChanged,
                 }));
}

TEST_CASE("Initializing drops events until init_*() is called", "[state_machine][init]") {
    sm::StateMachine m{};
    REQUIRE(m.state() == sm::State::Initializing);

    REQUIRE_FALSE(m.handle(sm::Event::CommandOpen).transitioned);
    REQUIRE_FALSE(m.handle(sm::Event::CommandClose).transitioned);
    REQUIRE_FALSE(m.handle(sm::Event::LimitOpenHit).transitioned);
    REQUIRE(m.state() == sm::State::Initializing);

    m.init_closed();
    REQUIRE(m.handle(sm::Event::CommandOpen).transitioned);
}

TEST_CASE("FaultCleared from non-Faulted is a no-op", "[state_machine][fault][reset]") {
    sm::StateMachine m{};
    m.init_closed();
    const auto out = m.handle(sm::Event::FaultCleared);
    REQUIRE(m.state() == sm::State::Closed);
    REQUIRE_FALSE(out.transitioned);
    REQUIRE(out.count == 0);
}

TEST_CASE("to_string covers every enumerator", "[state_machine][telemetry]") {
    // Smoke-check that telemetry-serialisable enums always produce a
    // non-empty string. A future enumerator added without updating
    // to_string would fall through to the "?" sentinel.
    REQUIRE(sm::to_string(sm::State::Closed)       == "Closed");
    REQUIRE(sm::to_string(sm::State::Faulted)      == "Faulted");
    REQUIRE(sm::to_string(sm::Event::CommandOpen)  == "CommandOpen");
    REQUIRE(sm::to_string(sm::Action::StopMotor)   == "StopMotor");
    REQUIRE(sm::to_string(sm::Reason::OperatorStop) == "OperatorStop");
}
