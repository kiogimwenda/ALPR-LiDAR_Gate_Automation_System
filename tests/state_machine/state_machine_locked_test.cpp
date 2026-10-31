// state_machine_locked_test.cpp — the lock-sequenced table, spec 8.4.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <random>
#include <vector>

#include "gate_state_machine/state_machine.hpp"

namespace sm = gate::state_machine;
using sm::Action;
using sm::Event;
using sm::OpenOrigin;
using sm::State;

namespace {

constexpr int kEventCount = 37;

sm::Config locked(sm::ActuatorProfile p = sm::ActuatorProfile::Operator) {
    return sm::Config{.lock_layout = sm::LockLayout::Five, .profile = p};
}

bool has(const sm::Outputs& o, Action a) {
    for (std::uint8_t i = 0; i < o.count; ++i) {
        if (o.actions[i] == a) {
            return true;
        }
    }
    return false;
}

bool drives(const sm::Outputs& o) {
    return has(o, Action::DriveMotorOpen) || has(o, Action::DriveMotorClose) ||
           has(o, Action::RunVerifyPulse) || has(o, Action::RunCalibration);
}

struct Place {
    const char* name;
    State state;
};

// Walks a machine into each state by legal events only.
sm::StateMachine at(State target) {
    sm::StateMachine m{locked()};
    const auto open = [&] {
        m.handle(Event::CommandOpen);
        m.handle(Event::LocksReleased);
        m.handle(Event::LimitClosedReleased);
    };
    switch (target) {
        case State::Initializing:
            break;
        case State::Faulted:
            m.init_unknown();
            break;
        case State::Closed:
            m.init_closed();
            break;
        case State::ClosedInsecure:
            m.init_closed(false);
            break;
        case State::Lockdown:
            m.init_closed();
            m.handle(Event::LockdownOn);
            break;
        case State::Unlocking:
            m.init_closed();
            m.handle(Event::CommandOpen);
            break;
        case State::Opening:
            m.init_closed();
            open();
            break;
        case State::Open:
            m.init_closed();
            open();
            m.handle(Event::LimitOpenHit);
            break;
        case State::StoppedOpen:
            m.init_closed();
            open();
            m.handle(Event::CommandStop);
            break;
        case State::PreClose:
            m.init_open();
            m.handle(Event::CommandClose);
            break;
        case State::Closing:
            m.init_open();
            m.handle(Event::CommandClose);
            m.handle(Event::LocksReleased);
            break;
        case State::StoppedClose:
            m.init_open();
            m.handle(Event::CommandClose);
            m.handle(Event::LocksReleased);
            m.handle(Event::CommandStop);
            break;
        case State::Locking:
            m.init_open();
            m.handle(Event::CommandClose);
            m.handle(Event::LocksReleased);
            m.handle(Event::LimitClosedHit);
            break;
        case State::Verifying:
            m.init_closed();
            m.handle(Event::BeginVerify);
            break;
        case State::Calibrating:
            m.init_closed();
            m.handle(Event::BeginCalibrate);
            break;
    }
    REQUIRE(m.state() == target);
    return m;
}

const std::array kAllStates = {
    State::Initializing, State::Closed,      State::Opening,        State::Open,
    State::Closing,      State::StoppedOpen, State::StoppedClose,   State::Faulted,
    State::Unlocking,    State::Locking,     State::ClosedInsecure, State::PreClose,
    State::Verifying,    State::Calibrating, State::Lockdown,
};

}  // namespace

TEST_CASE("full cycle sequences locks around motion", "[locked]") {
    sm::StateMachine m{locked()};
    m.init_closed();

    auto o = m.handle(Event::CommandOpen);
    CHECK(o.new_state == State::Unlocking);
    CHECK(has(o, Action::ReleaseLocks));
    CHECK_FALSE(drives(o));

    o = m.handle(Event::LocksReleased);
    CHECK(o.new_state == State::Opening);
    CHECK(has(o, Action::SetPermit));
    CHECK(has(o, Action::DriveMotorOpen));

    m.handle(Event::LimitClosedReleased);
    CHECK(m.handle(Event::LimitOpenHit).new_state == State::Open);

    o = m.handle(Event::CommandClose);
    CHECK(o.new_state == State::PreClose);
    CHECK(has(o, Action::RetractBolts));
    CHECK(m.handle(Event::LocksReleased).new_state == State::Closing);

    o = m.handle(Event::LimitClosedHit);
    CHECK(o.new_state == State::Locking);
    CHECK(has(o, Action::StopMotor));
    CHECK(has(o, Action::ClearPermit));
    CHECK(has(o, Action::EngageLocks));
    CHECK(m.handle(Event::LocksEngaged).new_state == State::Closed);
}

TEST_CASE("no motion action without LocksReleased, any random event walk", "[locked][property]") {
    std::mt19937 rng(20261031);
    std::uniform_int_distribution<int> ev(0, kEventCount - 1);
    std::uniform_int_distribution<int> coin(0, 1);
    const auto seed_state = GENERATE(State::Closed, State::ClosedInsecure, State::Lockdown);
    for (int walk = 0; walk < 2'000; ++walk) {
        sm::StateMachine m = at(seed_state);
        bool released = false;
        for (int i = 0; i < 60; ++i) {
            const auto e = static_cast<Event>(ev(rng));
            if (e == Event::FaultCleared || e == Event::LocalReset) {
                continue;  // needs a re-init by the driver layer
            }
            const auto o = m.handle(e, coin(rng) ? OpenOrigin::Rte : OpenOrigin::Remote);
            released = m.locks_free();
            if (drives(o)) {
                REQUIRE(released);
            }
            REQUIRE(o.count < sm::kMaxActionsPerStep);
        }
    }
}

TEST_CASE("every state handles every event without leaving the state set", "[locked]") {
    for (const State s : kAllStates) {
        for (int e = 0; e < kEventCount; ++e) {
            auto m = at(s);
            const auto o = m.handle(static_cast<Event>(e));
            CHECK(static_cast<int>(m.state()) <= static_cast<int>(State::Lockdown));
            CHECK(o.new_state == m.state());
        }
    }
}

TEST_CASE("no step fills the action buffer", "[locked]") {
    for (const State s : kAllStates) {
        for (int e = 0; e < kEventCount; ++e) {
            for (const auto origin : {OpenOrigin::Remote, OpenOrigin::Rte}) {
                for (const bool blocked : {false, true}) {
                    for (const bool latch : {false, true}) {
                        auto m = at(s);
                        if (blocked) {
                            m.handle(Event::SafetyBeamTripped);
                        }
                        if (latch) {
                            m.handle(Event::LockdownOn);
                        }
                        // Mid-travel Unlocking is the known worst case.
                        m.handle(Event::LimitClosedReleased);
                        CHECK(m.handle(static_cast<Event>(e), origin).count <
                              sm::kMaxActionsPerStep);
                    }
                }
            }
        }
    }
    auto m = at(State::StoppedOpen);
    m.handle(Event::BoltsRested);
    m.handle(Event::CommandOpen);
    REQUIRE(m.state() == State::Unlocking);
    const auto o = m.handle(Event::LockdownOn);
    CHECK(o.count >= 6);
    CHECK(has(o, Action::PersistLockdown));
    CHECK(has(o, Action::RetractBolts));
}

TEST_CASE("review findings", "[locked][regress]") {
    SECTION("a reversed lockdown close closes again at Open") {
        auto m = at(State::Open);
        m.handle(Event::LockdownOn);
        m.handle(Event::LocksReleased);
        REQUIRE(m.state() == State::Closing);
        m.handle(Event::SafetyBeamTripped);
        m.handle(Event::SafetyBeamCleared);
        m.handle(Event::LimitOpenHit);
        CHECK(m.state() == State::PreClose);
    }
    SECTION("key off retries a pending lockdown close") {
        auto m = at(State::Open);
        m.handle(Event::KeyOverrideOn);
        m.handle(Event::LockdownOn);
        CHECK(m.state() == State::Open);
        m.handle(Event::KeyOverrideOff);
        CHECK(m.state() == State::PreClose);
    }
    SECTION("insecure follows the limits when moved by hand") {
        auto m = at(State::Closed);
        m.handle(Event::KeyOverrideOn);
        m.handle(Event::LimitClosedReleased);
        CHECK(m.state() == State::StoppedOpen);
        m.handle(Event::KeyOverrideOff);
        m.handle(Event::CommandClose);
        CHECK(m.state() == State::PreClose);
    }
    SECTION("pushed back by hand is insecure") {
        auto m = at(State::StoppedOpen);
        m.handle(Event::LimitClosedHit);
        CHECK(m.state() == State::ClosedInsecure);
    }
    SECTION("key on while stopped forces a re-proof") {
        auto m = at(State::StoppedOpen);
        REQUIRE(m.locks_free());
        m.handle(Event::KeyOverrideOn);
        CHECK_FALSE(m.locks_free());
        m.handle(Event::KeyOverrideOff);
        const auto o = m.handle(Event::CommandClose);
        CHECK(m.state() == State::PreClose);
        CHECK_FALSE(drives(o));
    }
    SECTION("faults and key-on rest the bolts") {
        auto u = at(State::Unlocking);
        CHECK(has(u.handle(Event::LockFailed), Action::RestBolts));
        auto k = at(State::Unlocking);
        CHECK(has(k.handle(Event::KeyOverrideOn), Action::RestBolts));
        auto l = at(State::Locking);
        CHECK(has(l.handle(Event::KeyOverrideOn), Action::RestBolts));
        auto o = at(State::Opening);
        CHECK(has(o.handle(Event::MotorTimeout), Action::RestBolts));
    }
}

TEST_CASE("RTE in every state", "[locked][rte]") {
    const auto expected = [](State s) {
        switch (s) {
            case State::Closed:
            case State::ClosedInsecure:
            case State::Lockdown:
            case State::Locking:
            case State::Verifying:
            case State::Calibrating:
            case State::Faulted:
                return State::Unlocking;
            case State::Closing:
            case State::StoppedOpen:
            case State::StoppedClose:
                return State::Opening;
            case State::PreClose:
                return State::Open;
            default:
                return s;  // Opening, Open, Unlocking, Initializing
        }
    };
    for (const State s : kAllStates) {
        auto m = at(s);
        const auto o = m.handle(Event::CommandOpen, OpenOrigin::Rte);
        INFO(sm::to_string(s));
        CHECK(m.state() == expected(s));
        if (s == State::Initializing) {
            CHECK(o.reason == sm::Reason::Busy);  // never silent
        }
    }
}

TEST_CASE("RTE beats reserve but not cutoff; remote opens stop at reserve", "[locked][rte]") {
    auto m = at(State::Closed);
    m.set_battery(sm::BatteryLevel::BelowReserve);
    CHECK(m.handle(Event::CommandOpen).reason == sm::Reason::BatteryReserve);
    CHECK(m.state() == State::Closed);
    m.set_battery(sm::BatteryLevel::BelowCutoff);
    CHECK(m.handle(Event::CommandOpen, OpenOrigin::Rte).reason == sm::Reason::BatteryReserve);
    CHECK(m.state() == State::Closed);
    m.set_battery(sm::BatteryLevel::BelowReserve);
    m.handle(Event::CommandOpen, OpenOrigin::Rte);
    CHECK(m.state() == State::Unlocking);
}

TEST_CASE("fault classes per 7.3", "[locked][fault]") {
    SECTION("non-lock: RTE opens from unknown position") {
        auto m = at(State::Faulted);
        m.handle(Event::CommandOpen, OpenOrigin::Rte);
        CHECK(m.state() == State::Unlocking);
    }
    SECTION("lock: RTE runs one fresh retract test, failure stays faulted") {
        auto m = at(State::Unlocking);
        m.handle(Event::LockFailed);
        CHECK(m.state() == State::Faulted);
        CHECK(m.reason() == sm::Reason::LockJam);
        m.handle(Event::CommandOpen, OpenOrigin::Rte);
        CHECK(m.state() == State::Unlocking);
        m.handle(Event::LockFailed);
        CHECK(m.state() == State::Faulted);
    }
    SECTION("hardware: RTE is logged only, local reset refused") {
        auto m = at(State::Closed);
        m.handle(Event::PermitFault);
        CHECK(m.reason() == sm::Reason::PermitFault);
        const auto o = m.handle(Event::CommandOpen, OpenOrigin::Rte);
        CHECK(m.state() == State::Faulted);
        CHECK(has(o, Action::RaiseAlert));
        m.handle(Event::LocalReset);
        CHECK(m.state() == State::Faulted);
    }
    SECTION("both limits asserted is hardware class") {
        sm::StateMachine m{locked()};
        m.init_limit_conflict();
        m.handle(Event::LocalReset);
        CHECK(m.state() == State::Faulted);
    }
    SECTION("local reset clears lock class") {
        auto m = at(State::Unlocking);
        m.handle(Event::LockFailed);
        m.handle(Event::LocalReset);
        CHECK(m.state() == State::Initializing);
    }
    SECTION("remote open never clears a fault") {
        auto m = at(State::Faulted);
        m.handle(Event::CommandOpen);
        CHECK(m.state() == State::Faulted);
    }
}

TEST_CASE("lockdown", "[locked][lockdown]") {
    SECTION("refuses remote opens") {
        auto m = at(State::Lockdown);
        CHECK(m.handle(Event::CommandOpen).reason == sm::Reason::Lockdown);
        CHECK(m.state() == State::Lockdown);
    }
    SECTION("latch persists through an RTE cycle and returns to Lockdown") {
        auto m = at(State::Lockdown);
        const auto o = m.handle(Event::CommandOpen, OpenOrigin::Rte);
        CHECK(m.state() == State::Unlocking);
        CHECK(has(o, Action::RaiseAlert));
        m.handle(Event::LocksReleased);
        m.handle(Event::LimitClosedReleased);
        m.handle(Event::LimitOpenHit);
        CHECK(m.state() == State::Open);  // the exit finishes
        CHECK(m.lockdown_latched());
        m.handle(Event::CommandClose);
        m.handle(Event::LocksReleased);
        m.handle(Event::LimitClosedHit);
        m.handle(Event::LocksEngaged);
        CHECK(m.state() == State::Lockdown);
        CHECK(m.lockdown_latched());
    }
    SECTION("survives a reboot") {
        auto before = at(State::Lockdown);
        sm::StateMachine m{locked()};
        m.restore_lockdown(before.lockdown_latched());
        m.init_closed();
        CHECK(m.state() == State::Lockdown);
    }
    SECTION("only LockdownOff clears") {
        auto m = at(State::Lockdown);
        const auto o = m.handle(Event::LockdownOff);
        CHECK(m.state() == State::Closed);
        CHECK(has(o, Action::PersistLockdown));
        CHECK_FALSE(m.lockdown_latched());
    }
    SECTION("from Open closes when the beam allows") {
        auto m = at(State::Open);
        m.handle(Event::SafetyBeamTripped);
        m.handle(Event::LockdownOn);
        CHECK(m.state() == State::Open);
        m.handle(Event::SafetyBeamCleared);
        CHECK(m.state() == State::PreClose);
    }
    SECTION("from Opening closes on arrival") {
        auto m = at(State::Opening);
        m.handle(Event::LockdownOn);
        CHECK(m.state() == State::Opening);
        m.handle(Event::LimitOpenHit);
        CHECK(m.state() == State::PreClose);
    }
    SECTION("from Unlocking aborts to Locking") {
        auto m = at(State::Unlocking);
        m.handle(Event::LockdownOn);
        CHECK(m.state() == State::Locking);
        m.handle(Event::LocksEngaged);
        CHECK(m.state() == State::Lockdown);
    }
    SECTION("closing states end in Lockdown") {
        auto s = GENERATE(State::Closing, State::PreClose, State::Locking);
        auto m = at(s);
        m.handle(Event::LockdownOn);
        CHECK(m.state() == s);
        if (s == State::PreClose) {
            m.handle(Event::LocksReleased);
        }
        if (s != State::Locking) {
            m.handle(Event::LimitClosedHit);
        }
        m.handle(Event::LocksEngaged);
        CHECK(m.state() == State::Lockdown);
    }
    SECTION("insecure and faulted only latch") {
        auto m = at(State::ClosedInsecure);
        m.handle(Event::LockdownOn);
        CHECK(m.state() == State::ClosedInsecure);
        m.handle(Event::LocksEngaged);
        CHECK(m.state() == State::Lockdown);
        auto f = at(State::Faulted);
        f.handle(Event::LockdownOn);
        CHECK(f.state() == State::Faulted);
        CHECK(f.lockdown_latched());
    }
    SECTION("an RTE open under way is not turned round") {
        auto m = at(State::Lockdown);
        m.handle(Event::CommandOpen, OpenOrigin::Rte);
        m.handle(Event::LockdownOn);
        CHECK(m.state() == State::Unlocking);
    }
    SECTION("verify aborts and closes") {
        auto m = at(State::Verifying);
        m.handle(Event::LockdownOn);
        CHECK(m.state() == State::Locking);
    }
}

TEST_CASE("battery critical close is beam guarded", "[locked][battery]") {
    SECTION("blocked beam never closes, retry closes once clear") {
        auto s = GENERATE(State::Open, State::StoppedOpen, State::StoppedClose);
        auto m = at(s);
        m.handle(Event::SafetyBeamTripped);
        const auto o = m.handle(Event::BatteryCritical);
        CHECK(m.state() == s);
        CHECK(o.reason == sm::Reason::SafetyBeamObstacle);
        CHECK_FALSE(drives(o));
        m.handle(Event::SafetyBeamCleared);
        CHECK(m.state() == s);  // the driver's 10 s retry acts, not the clear
        m.handle(Event::BatteryCritical);
        CHECK((m.state() == State::PreClose || m.state() == State::Closing));
    }
    SECTION("a beam trip during PreClose stops the close starting") {
        auto m = at(State::Open);
        m.handle(Event::BatteryCritical);
        m.handle(Event::SafetyBeamTripped);
        const auto o = m.handle(Event::LocksReleased);
        CHECK(m.state() == State::Open);
        CHECK_FALSE(drives(o));
    }
}

TEST_CASE("operator profile follows reversal, sends no command", "[locked][reversal]") {
    SECTION("beam during closing") {
        auto m = at(State::Closing);
        const auto o = m.handle(Event::SafetyBeamTripped);
        CHECK(m.state() == State::Opening);
        CHECK_FALSE(has(o, Action::DriveMotorOpen));
        CHECK_FALSE(has(o, Action::StopMotor));
    }
    SECTION("contactor profile stops as before") {
        sm::StateMachine m{locked(sm::ActuatorProfile::Contactor)};
        m.init_open();
        m.handle(Event::CommandClose);
        m.handle(Event::LocksReleased);
        m.handle(Event::SafetyBeamTripped);
        CHECK(m.state() == State::StoppedClose);
    }
    SECTION("reversal and collision") {
        auto m = at(State::Closing);
        auto o = m.handle(Event::OperatorCollision);
        CHECK(m.state() == State::Closing);
        CHECK(o.count == 1);
        o = m.handle(Event::OperatorReversed);
        CHECK(m.state() == State::Opening);
        CHECK_FALSE(drives(o));
        CHECK_FALSE(has(o, Action::StopMotor));
    }
    SECTION("overrun only alerts") {
        auto m = at(State::Opening);
        const auto o = m.handle(Event::CurrentOverrun);
        CHECK(m.state() == State::Opening);
        CHECK_FALSE(has(o, Action::StopMotor));
        CHECK(has(o, Action::RaiseAlert));
    }
    SECTION("direction and no-response fault") {
        auto e = GENERATE(Event::OperatorDirectionWrong, Event::OperatorNoResponse);
        auto m = at(State::Opening);
        m.handle(e);
        CHECK(m.state() == State::Faulted);
        CHECK(m.reason() == sm::Reason::OperatorFault);
    }
}

TEST_CASE("key override", "[locked][key]") {
    SECTION("closed goes insecure without a critical reason, then relocks") {
        auto m = at(State::Closed);
        m.handle(Event::KeyOverrideOn);
        CHECK(m.state() == State::ClosedInsecure);
        CHECK(m.reason() == sm::Reason::KeyOverride);
        CHECK(m.handle(Event::CommandOpen, OpenOrigin::Rte).reason == sm::Reason::KeyOverride);
        m.handle(Event::KeyOverrideOff);
        CHECK(m.state() == State::Locking);
    }
    SECTION("lock loss under key is an override, without key it is an attack") {
        auto m = at(State::Closed);
        m.handle(Event::GateForced);
        CHECK(m.reason() == sm::Reason::ForcedEntry);
    }
    SECTION("moving stops") {
        auto m = at(State::Closing);
        const auto o = m.handle(Event::KeyOverrideOn);
        CHECK(m.state() == State::StoppedClose);
        CHECK(has(o, Action::ClearPermit));
    }
    SECTION("lockdown latch stays through an override") {
        auto m = at(State::Lockdown);
        m.handle(Event::KeyOverrideOn);
        CHECK(m.state() == State::ClosedInsecure);
        CHECK(m.lockdown_latched());
        m.handle(Event::KeyOverrideOff);
        m.handle(Event::LocksEngaged);
        CHECK(m.state() == State::Lockdown);
    }
    SECTION("no auto-resume on release") {
        auto m = at(State::StoppedOpen);
        m.handle(Event::KeyOverrideOn);
        m.handle(Event::KeyOverrideOff);
        CHECK(m.state() == State::StoppedOpen);
    }
}

TEST_CASE("lock failures", "[locked]") {
    SECTION("unlock failure restores locks and refuses motion") {
        auto m = at(State::Unlocking);
        const auto o = m.handle(Event::LockFailed);
        CHECK(m.state() == State::Faulted);
        CHECK(has(o, Action::EngageLocks));
        CHECK_FALSE(drives(o));
    }
    SECTION("feedback fault is recorded as such") {
        auto m = at(State::Unlocking);
        m.handle(Event::LockFeedbackFault);
        CHECK(m.reason() == sm::Reason::LockFeedback);
    }
    SECTION("pre-close failure leaves the gate open") {
        auto m = at(State::PreClose);
        m.handle(Event::LockFailed);
        CHECK(m.state() == State::Open);
    }
    SECTION("lock failure ends insecure, retry reaches Closed") {
        auto m = at(State::Locking);
        m.handle(Event::LockFailed);
        CHECK(m.state() == State::ClosedInsecure);
        CHECK(m.reason() == sm::Reason::LockIncomplete);
        m.handle(Event::LocksEngaged);
        CHECK(m.state() == State::Closed);
    }
    SECTION("tamper while closed") {
        auto m = at(State::Closed);
        const auto o = m.handle(Event::LockDropped);
        CHECK(m.state() == State::ClosedInsecure);
        CHECK(has(o, Action::RaiseAlert));
    }
}

TEST_CASE("bolts rest during a long open", "[locked]") {
    auto m = at(State::Open);
    REQUIRE(m.locks_free());
    m.handle(Event::BoltsRested);
    CHECK_FALSE(m.locks_free());
    m.handle(Event::CommandClose);
    CHECK(m.state() == State::PreClose);

    auto s = at(State::StoppedOpen);
    s.handle(Event::BoltsRested);
    s.handle(Event::CommandOpen);
    CHECK(s.state() == State::Unlocking);
}

TEST_CASE("verify and calibrate", "[locked]") {
    SECTION("mismatch closes, locks, alerts") {
        auto m = at(State::Verifying);
        CHECK(has(m.handle(Event::LocksReleased), Action::RunVerifyPulse));
        m.handle(Event::LimitClosedReleased);
        const auto o = m.handle(Event::VerifyFailed);
        CHECK(m.state() == State::Closing);
        CHECK(m.reason() == sm::Reason::GateTypeMismatch);
        CHECK(has(o, Action::RaiseAlert));
        m.handle(Event::LimitClosedHit);
        CHECK(m.state() == State::Locking);
    }
    SECTION("pass hands on to calibration") {
        auto m = at(State::Verifying);
        m.handle(Event::LocksReleased);
        CHECK(has(m.handle(Event::BeginCalibrate), Action::RunCalibration));
        CHECK(m.state() == State::Calibrating);
        m.handle(Event::CalibrateDone);
        CHECK(m.state() == State::Locking);
    }
    SECTION("remote open refused busy, RTE aborts") {
        auto m = at(State::Calibrating);
        CHECK(m.handle(Event::CommandOpen).reason == sm::Reason::Busy);
        m.handle(Event::LocksReleased);
        m.handle(Event::CommandOpen, OpenOrigin::Rte);
        CHECK(m.state() == State::Opening);
    }
    SECTION("refused with the beam blocked") {
        auto m = at(State::Closed);
        m.handle(Event::SafetyBeamTripped);
        m.handle(Event::BeginVerify);
        CHECK(m.state() == State::Closed);
    }
}

TEST_CASE("legacy layout drops every new event", "[locked][legacy]") {
    sm::StateMachine m{};
    m.init_closed();
    for (int e = static_cast<int>(Event::LocksReleased); e < kEventCount; ++e) {
        const auto o = m.handle(static_cast<Event>(e));
        CHECK_FALSE(o.transitioned);
    }
    CHECK(m.state() == State::Closed);
    CHECK_FALSE(m.lockdown_latched());
}
