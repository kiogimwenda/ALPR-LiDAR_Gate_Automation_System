// gate_supervisor_test.cpp — controller logic against host fakes.

#include "gate_state_machine/gate_supervisor.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace sm = gate::state_machine;
using sm::Event;
using sm::OperatorStatus;
using sm::State;

namespace {

struct FakePin final : sm::OutputPin {
    bool on = false;
    int rises = 0;
    void set(bool v) override {
        rises += (v && !on) ? 1 : 0;
        on = v;
    }
};

// Bolts and maglocks follow their drive after one tick.
struct FakeLocks final : sm::LockIo {
    bool bolts = false;
    bool mags = true;
    bool permit = false;
    int permit_drops = 0;
    bool jam = false;
    bool readback_stuck = false;
    void drive(bool b, bool m) override {
        bolts = b;
        mags = m;
    }
    sm::LockFeedback feedback() override {
        sm::LockFeedback f;
        const bool out = bolts && !jam;
        f.bolt_retracted = {out, out};
        f.bolt_extended = {!out, !out};
        f.mag_bonded = {mags, mags, false};
        return f;
    }
    void set_permit(bool on) override {
        permit_drops += (permit && !on) ? 1 : 0;
        permit = on;
    }
    bool permit_readback() override { return readback_stuck ? false : permit; }
};

struct Rig {
    FakeLocks locks;
    FakePin frx;
    FakePin trg;
    sm::OperatorActuator actuator{frx, trg};
    sm::StateMachine machine;
    sm::GateSupervisor sup;
    std::uint64_t now = 1;
    int status_phase = 0;  // 0 dark, 1 slow flash, 2 fast flash
    int moving_events = 0;
    int reversed = 0;

    explicit Rig(sm::GateSupervisor::Config c = {})
        : machine(sm::Config{.lock_layout = sm::LockLayout::Four,
                             .profile = sm::ActuatorProfile::Operator}),
          sup(c, locks, actuator) {
        sup.apply(machine.init_closed(), now);
        sup.assume_engaged();
    }
    void step(Event e) {
        sup.observe(e, now);
        sup.apply(machine.handle(e), now);
    }
    bool lit() const {
        if (status_phase == 1) {
            return now % 1000 < 500;
        }
        if (status_phase == 2) {
            return now % 250 < 125;
        }
        return false;
    }
    void run(std::uint32_t ms) {
        for (std::uint32_t t = 0; t < ms; t += 20) {
            now += 20;
            const auto ev = sup.tick(now, lit());
            for (std::size_t i = 0; i < ev.count; ++i) {
                moving_events += ev.items[i] == Event::OperatorMoving ? 1 : 0;
                reversed += ev.items[i] == Event::OperatorReversed ? 1 : 0;
                step(ev.items[i]);
            }
        }
    }
};

sm::OperatorStatus decode(std::uint32_t on, std::uint32_t off, int repeats) {
    sm::StatusDecoder d;
    for (int r = 0; r < repeats; ++r) {
        for (std::uint32_t t = 0; t < on; t += 20) {
            d.sample(true);
        }
        for (std::uint32_t t = 0; t < off; t += 20) {
            d.sample(false);
        }
    }
    return d.status();
}

sm::OperatorStatus group(int flashes, int repeats = 2) {
    sm::StatusDecoder d;
    for (int r = 0; r < repeats; ++r) {
        for (int f = 0; f < flashes; ++f) {
            for (int i = 0; i < 5; ++i) {
                d.sample(true);
            }
            for (int i = 0; i < 10; ++i) {
                d.sample(false);
            }
        }
        for (int i = 0; i < 60; ++i) {
            d.sample(false);
        }
    }
    return d.status();
}

}  // namespace

TEST_CASE("status decoder patterns", "[status_decoder]") {
    CHECK(decode(0, 3'000, 1) == OperatorStatus::Closed);
    CHECK(decode(3'000, 0, 1) == OperatorStatus::OpenOrPart);
    CHECK(decode(500, 500, 3) == OperatorStatus::Opening);
    CHECK(decode(125, 125, 6) == OperatorStatus::Closing);
    CHECK(decode(400, 600, 3) == OperatorStatus::Opening);  // jitter
    CHECK(decode(100, 150, 6) == OperatorStatus::Closing);
    CHECK(group(2) == OperatorStatus::NoMains);
    CHECK(group(3) == OperatorStatus::LowBattery);
    CHECK(group(4) == OperatorStatus::MultiCollision);
    CHECK(group(1) == OperatorStatus::Unknown);  // pillar light ignored
}

TEST_CASE("full cycle pulses FRX and TRG, never holds them", "[supervisor]") {
    Rig r;
    r.step(Event::CommandOpen);
    r.run(200);
    CHECK_FALSE(r.locks.mags);
    CHECK_FALSE(r.frx.on);  // no drive before the locks prove out
    r.status_phase = 1;
    r.run(600);
    REQUIRE(r.machine.state() == State::Opening);
    CHECK(r.locks.permit);
    CHECK(r.frx.rises == 1);
    r.run(600);
    CHECK_FALSE(r.frx.on);
    r.run(1'500);
    CHECK(r.moving_events == 1);

    r.status_phase = 0;
    r.step(Event::LimitClosedReleased);
    r.step(Event::LimitOpenHit);
    r.step(Event::CommandClose);
    r.run(400);
    REQUIRE(r.machine.state() == State::Closing);
    CHECK(r.trg.rises == 1);
    r.status_phase = 2;
    r.run(1'500);
    r.step(Event::LimitClosedHit);
    CHECK_FALSE(r.locks.permit);
    r.run(1'000);
    CHECK(r.machine.state() == State::Closed);
    CHECK_FALSE(r.locks.bolts);
    CHECK(r.locks.mags);
}

TEST_CASE("jam faults with no pulse", "[supervisor]") {
    Rig r;
    r.locks.jam = true;
    r.step(Event::CommandOpen);
    r.run(5'000);
    CHECK(r.machine.state() == State::Faulted);
    CHECK(r.frx.rises == 0);
    CHECK_FALSE(r.locks.permit);
    CHECK_FALSE(r.locks.bolts);
}

TEST_CASE("no response retries once then faults", "[supervisor]") {
    Rig r;
    r.step(Event::CommandOpen);
    r.run(6'000);
    CHECK(r.frx.rises == 2);
    CHECK(r.machine.state() == State::Faulted);
    CHECK(r.machine.reason() == sm::Reason::OperatorFault);
}

TEST_CASE("wrong direction in the start window faults", "[supervisor]") {
    Rig r;
    r.status_phase = 2;
    r.step(Event::CommandOpen);
    r.run(3'000);
    CHECK(r.machine.state() == State::Faulted);
}

TEST_CASE("beam reversal: no second command, no stop, no fault", "[supervisor]") {
    Rig r;
    r.step(Event::CommandOpen);
    r.status_phase = 1;
    r.run(2'500);
    r.step(Event::LimitClosedReleased);
    r.status_phase = 0;
    r.step(Event::LimitOpenHit);
    r.step(Event::CommandClose);
    r.status_phase = 2;
    r.run(2'000);
    REQUIRE(r.machine.state() == State::Closing);
    const int drops = r.locks.permit_drops;
    r.step(Event::SafetyBeamTripped);
    r.status_phase = 1;
    r.run(2'500);
    CHECK(r.machine.state() == State::Opening);
    CHECK(r.frx.rises == 1);  // only the original open
    CHECK(r.locks.permit_drops == drops);
    CHECK(r.reversed == 0);
}

TEST_CASE("operator-initiated reversal is followed", "[supervisor]") {
    Rig r;
    r.step(Event::CommandOpen);
    r.status_phase = 1;
    r.run(2'500);
    REQUIRE(r.machine.state() == State::Opening);
    r.status_phase = 2;
    r.run(2'000);
    CHECK(r.reversed == 1);
    CHECK(r.machine.state() == State::Closing);
    CHECK(r.trg.rises == 0);
}

TEST_CASE("human stop drops the permit, then re-arms", "[supervisor]") {
    Rig r;
    r.step(Event::CommandOpen);
    r.status_phase = 1;
    r.run(2'500);
    r.step(Event::CommandStop);
    CHECK(r.machine.state() == State::StoppedOpen);
    CHECK_FALSE(r.locks.permit);
    r.status_phase = 0;
    r.run(300);
    CHECK(r.locks.permit);
}

TEST_CASE("permit readback mismatch faults", "[supervisor]") {
    Rig r;
    r.locks.readback_stuck = true;
    r.step(Event::CommandOpen);
    r.status_phase = 1;
    r.run(1'500);
    CHECK(r.machine.state() == State::Faulted);
    CHECK(r.machine.reason() == sm::Reason::PermitFault);
}

TEST_CASE("bolts rest after the hold time", "[supervisor]") {
    sm::GateSupervisor::Config c;
    c.bolt_hold_open_ms = 1'000;
    Rig r(c);
    r.step(Event::CommandOpen);
    r.status_phase = 1;
    r.run(2'000);
    r.status_phase = 0;
    r.step(Event::LimitOpenHit);
    r.run(1'200);
    CHECK_FALSE(r.locks.bolts);
    CHECK_FALSE(r.locks.permit);
    CHECK_FALSE(r.machine.locks_free());
}

TEST_CASE("insecure relocks on the retry timer", "[supervisor]") {
    sm::GateSupervisor::Config c;
    c.relock_retry_ms = 1'000;
    Rig r(c);
    r.step(Event::LockDropped);
    REQUIRE(r.machine.state() == State::ClosedInsecure);
    r.run(4'000);
    CHECK(r.machine.state() == State::Closed);
}

TEST_CASE("the guard defers permit and bolt drops", "[supervisor]") {
    const auto closing = [](Rig& r) {
        r.step(Event::CommandOpen);
        r.status_phase = 1;
        r.run(2'500);
        r.status_phase = 0;
        r.step(Event::LimitOpenHit);
        r.step(Event::CommandClose);
        r.status_phase = 2;
        r.run(2'000);
        REQUIRE(r.machine.state() == State::Closing);
        r.step(Event::SafetyBeamTripped);
    };
    SECTION("a fault waits for the guard, then drops both") {
        Rig r;
        closing(r);
        r.step(Event::MotorTimeout);
        REQUIRE(r.machine.state() == State::Faulted);
        CHECK(r.locks.permit);
        CHECK(r.locks.bolts);
        r.status_phase = 0;
        r.run(3'200);
        CHECK_FALSE(r.locks.permit);
        CHECK_FALSE(r.locks.bolts);
    }
    SECTION("key and human stop go at once") {
        const Event e = GENERATE(Event::KeyOverrideOn, Event::CommandStop);
        Rig r;
        closing(r);
        r.step(e);
        CHECK_FALSE(r.locks.permit);
    }
    SECTION("permit fault goes at once") {
        Rig r;
        closing(r);
        r.step(Event::PermitFault);
        CHECK_FALSE(r.locks.permit);
        CHECK_FALSE(r.locks.bolts);
    }
}

TEST_CASE("verify and calibration never move the gate", "[supervisor]") {
    CHECK_FALSE(sm::GateSupervisor::admits(Event::BeginVerify));
    CHECK_FALSE(sm::GateSupervisor::admits(Event::BeginCalibrate));
    Rig r;
    r.step(Event::BeginVerify);
    r.run(2'000);
    CHECK(r.frx.rises == 0);
    CHECK(r.machine.state() == State::Closed);
}

TEST_CASE("a command that beats BoltsRested re-proves first", "[supervisor]") {
    sm::GateSupervisor::Config c;
    c.bolt_hold_open_ms = 100;
    Rig r(c);
    r.step(Event::CommandOpen);
    r.status_phase = 1;
    r.run(1'000);
    r.step(Event::CommandStop);
    r.status_phase = 0;
    REQUIRE(r.machine.state() == State::StoppedOpen);
    // Expiry without delivering BoltsRested, as if queued behind the open.
    for (int i = 0; i < 12; ++i) {
        r.now += 20;
        (void)r.sup.tick(r.now, false);
    }
    REQUIRE_FALSE(r.locks.bolts);
    const int before = r.frx.rises;
    r.step(Event::CommandOpen);
    CHECK(r.machine.state() == State::Opening);
    CHECK_FALSE(r.locks.permit);
    CHECK(r.frx.rises == before);
    r.run(300);  // includes pre_move_ms
    CHECK(r.locks.bolts);
    CHECK(r.locks.permit);
    CHECK(r.frx.rises == before + 1);
}

TEST_CASE("no relock while the key is active", "[supervisor]") {
    sm::GateSupervisor::Config c;
    c.relock_retry_ms = 500;
    Rig r(c);
    r.step(Event::KeyOverrideOn);
    r.run(3'000);
    CHECK(r.machine.state() == State::ClosedInsecure);
    r.step(Event::KeyOverrideOff);
    r.run(2'000);
    CHECK(r.machine.state() == State::Closed);
}

TEST_CASE("contactor actuator holds the relay", "[supervisor]") {
    FakePin o;
    FakePin c;
    sm::ContactorActuator a(o, c);
    a.open(0);
    a.tick(10'000);
    CHECK(o.on);
    a.close(0);
    CHECK_FALSE(o.on);
    CHECK(c.on);
    a.stop();
    CHECK_FALSE(c.on);
}
