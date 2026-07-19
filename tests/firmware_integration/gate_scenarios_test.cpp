// gate_scenarios_test.cpp — end-to-end scenarios over the pure firmware
// modules, composed exactly the way the ESP32-S3 wires them.
//
// SimulatedGate replays, in pure C++, the contracts that
// gate_control/gate_rpc implement on-target:
//
//   - GateController::execute        → motor/LED bookkeeping
//   - GateController::manage_timers  → watchdog + auto-close (tick counts)
//   - GateController::apply_policies → reverse-on-beam, auto-close re-arm
//   - the transition listener        → CommandTracker::on_gate_event
//   - the pump's 250 ms tick         → CommandTracker::on_tick
//
// plus a toy physics model: an energised motor reaches its limit
// switch after `travel_ticks`. The unit suites pin each module alone;
// these scenarios pin the *composition* — the places where a contract
// drift between modules would hide (and where writing them immediately
// found one: CommandTracker's mid-travel abort rule).

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>
#include <vector>

#include "gate_rpc/command_tracker.hpp"
#include "gate_state_machine/state_machine.hpp"

namespace sm = gate::state_machine;
namespace rpc = gate::rpc;

namespace {

struct Ack {
    std::string command_id;
    bool success;
    std::string error;
};

struct SimulatedGate {
    // One tick ≈ 100 ms of simulated time.
    static constexpr int kTravelTicks = 10;    // full travel: 1 s
    static constexpr int kWatchdogTicks = 20;  // motor budget: 2 s
    static constexpr int kAutoCloseTicks = 5;  // dwell: 0.5 s
    static constexpr std::uint32_t kCommandDeadlineMs = 4'000;

    explicit SimulatedGate(bool auto_close = false, bool reverse_on_beam = true)
        : auto_close_enabled(auto_close), reverse_on_beam(reverse_on_beam) {}

    sm::StateMachine machine;
    rpc::CommandTracker tracker;

    bool auto_close_enabled;
    bool reverse_on_beam;

    bool motor_open = false;
    bool motor_close = false;
    bool beam_blocked = false;
    int travel_remaining = -1;
    int watchdog_remaining = -1;
    int autoclose_remaining = -1;
    std::uint32_t now_ms = 0;
    std::vector<Ack> acks;

    void boot_closed() { apply(machine.init_closed()); }

    // Mirrors ControlClient::handle_command for an accepted async
    // motion command: arm the tracker, then issue the event.
    void server_command(const char* id, sm::Event ev, sm::State terminal) {
        const auto old = tracker.arm(id, terminal, now_ms + kCommandDeadlineMs);
        record(old);
        step(ev);
    }

    void step(sm::Event ev) { apply(machine.handle(ev)); }

    // Advance simulated time by one tick.
    void tick() {
        now_ms += 100;
        record(tracker.on_tick(now_ms));
        if ((motor_open || motor_close) && travel_remaining > 0 && --travel_remaining == 0) {
            step(motor_open ? sm::Event::LimitOpenHit : sm::Event::LimitClosedHit);
            return;  // limit event may have restarted timers; done this tick
        }
        if (watchdog_remaining > 0 && --watchdog_remaining == 0) {
            step(sm::Event::MotorTimeout);
            return;
        }
        if (autoclose_remaining > 0 && --autoclose_remaining == 0) {
            step(sm::Event::CommandClose);
        }
    }

    void run_ticks(int n) {
        for (int i = 0; i < n; ++i) {
            tick();
        }
    }

    void trip_beam() {
        beam_blocked = true;
        step(sm::Event::SafetyBeamTripped);
    }
    void clear_beam() {
        beam_blocked = false;
        step(sm::Event::SafetyBeamCleared);
    }

private:
    void record(const rpc::CommandTracker::Verdict& v) {
        if (v.fire) {
            acks.push_back({v.command_id, v.success, v.error});
        }
    }

    // GateController::execute + manage_timers + listener + policies,
    // in the same order the firmware runs them.
    void apply(const sm::Outputs& out) {
        for (std::uint8_t i = 0; i < out.count; ++i) {
            switch (out.actions[i]) {
                case sm::Action::DriveMotorOpen:
                    motor_close = false;
                    motor_open = true;
                    travel_remaining = kTravelTicks;
                    break;
                case sm::Action::DriveMotorClose:
                    motor_open = false;
                    motor_close = true;
                    travel_remaining = kTravelTicks;
                    break;
                case sm::Action::StopMotor:
                    motor_open = false;
                    motor_close = false;
                    travel_remaining = -1;
                    break;
                default:
                    break;  // LEDs / telemetry — not modelled
            }
        }

        if (out.transitioned) {
            const bool moving =
                out.new_state == sm::State::Opening || out.new_state == sm::State::Closing;
            watchdog_remaining = moving ? kWatchdogTicks : -1;
            autoclose_remaining =
                (out.new_state == sm::State::Open && auto_close_enabled) ? kAutoCloseTicks : -1;
        }

        if (out.transitioned || out.reason != sm::Reason::None) {
            record(tracker.on_gate_event(out.new_state, out.reason, out.transitioned));
        }

        // Policy 1: reverse away from a beam break during close.
        if (reverse_on_beam && out.transitioned && out.new_state == sm::State::StoppedClose &&
            out.reason == sm::Reason::SafetyBeamObstacle) {
            step(sm::Event::CommandOpen);
            return;
        }
        // Policy 2: an auto-close rejected by the beam latch re-arms.
        if (!out.transitioned && out.new_state == sm::State::Open &&
            out.reason == sm::Reason::SafetyBeamObstacle && auto_close_enabled) {
            autoclose_remaining = kAutoCloseTicks;
        }
    }
};

}  // namespace

TEST_CASE("remote open: command → travel → limit → success ack") {
    SimulatedGate g;
    g.boot_closed();
    REQUIRE(g.machine.state() == sm::State::Closed);

    g.server_command("cmd-open", sm::Event::CommandOpen, sm::State::Open);
    CHECK(g.machine.state() == sm::State::Opening);
    CHECK(g.motor_open);

    g.run_ticks(SimulatedGate::kTravelTicks);
    CHECK(g.machine.state() == sm::State::Open);
    CHECK_FALSE(g.motor_open);

    REQUIRE(g.acks.size() == 1);
    CHECK(g.acks[0].command_id == "cmd-open");
    CHECK(g.acks[0].success);
}

TEST_CASE("auto-close: gate closes itself after the dwell, no stray acks") {
    SimulatedGate g{/*auto_close=*/true};
    g.boot_closed();
    g.server_command("cmd-open", sm::Event::CommandOpen, sm::State::Open);
    g.run_ticks(SimulatedGate::kTravelTicks);
    REQUIRE(g.machine.state() == sm::State::Open);

    // Dwell expires → CommandClose → travel → Closed.
    g.run_ticks(SimulatedGate::kAutoCloseTicks + SimulatedGate::kTravelTicks);
    CHECK(g.machine.state() == sm::State::Closed);
    CHECK_FALSE(g.motor_close);
    CHECK(g.acks.size() == 1);  // only the open command ever acked
}

TEST_CASE("beam trip mid-close: abort ack, reverse to open, retry closes the loop") {
    SimulatedGate g{/*auto_close=*/true};
    g.boot_closed();
    g.server_command("cmd-open", sm::Event::CommandOpen, sm::State::Open);
    g.run_ticks(SimulatedGate::kTravelTicks);
    g.acks.clear();

    // Server orders a close; someone walks into the beam mid-travel.
    g.server_command("cmd-close", sm::Event::CommandClose, sm::State::Closed);
    REQUIRE(g.machine.state() == sm::State::Closing);
    g.run_ticks(3);
    g.trip_beam();

    // The command failed immediately with the abort reason (tracker
    // rule 3 — the rule this very scenario forced into existence)…
    REQUIRE(g.acks.size() == 1);
    CHECK(g.acks[0].command_id == "cmd-close");
    CHECK_FALSE(g.acks[0].success);
    CHECK(g.acks[0].error == "SafetyBeamObstacle");

    // …and reverse-on-beam drove the gate back open.
    CHECK(g.machine.state() == sm::State::Opening);
    g.run_ticks(SimulatedGate::kTravelTicks);
    REQUIRE(g.machine.state() == sm::State::Open);

    // Auto-close fires with the beam still blocked → latched rejection
    // → policy re-arms. After the beam clears, the next attempt lands.
    g.run_ticks(SimulatedGate::kAutoCloseTicks);
    CHECK(g.machine.state() == sm::State::Open);  // rejected, still open
    g.clear_beam();
    g.run_ticks(SimulatedGate::kAutoCloseTicks + SimulatedGate::kTravelTicks);
    CHECK(g.machine.state() == sm::State::Closed);
    CHECK(g.acks.size() == 1);  // the retries were policy, not commands
}

TEST_CASE("motor stall: watchdog faults the gate and fails the command") {
    SimulatedGate g;
    g.boot_closed();
    g.server_command("cmd-open", sm::Event::CommandOpen, sm::State::Open);

    // Sabotage the physics: the limit switch never engages.
    g.travel_remaining = -1;

    g.run_ticks(SimulatedGate::kWatchdogTicks);
    CHECK(g.machine.state() == sm::State::Faulted);
    CHECK(g.machine.reason() == sm::Reason::MotorTimeout);
    CHECK_FALSE(g.motor_open);

    REQUIRE(g.acks.size() == 1);
    CHECK_FALSE(g.acks[0].success);
    CHECK(g.acks[0].error == "MotorTimeout");

    // Recovery contract: FaultCleared lands in Initializing, where the
    // driver layer must re-verify the physical position (4.5.2).
    g.step(sm::Event::FaultCleared);
    CHECK(g.machine.state() == sm::State::Initializing);
}

TEST_CASE("operator stop mid-open aborts the command; a fresh open resumes") {
    SimulatedGate g;
    g.boot_closed();
    g.server_command("cmd-open", sm::Event::CommandOpen, sm::State::Open);
    g.run_ticks(4);

    g.step(sm::Event::CommandStop);
    CHECK(g.machine.state() == sm::State::StoppedOpen);
    REQUIRE(g.acks.size() == 1);
    CHECK_FALSE(g.acks[0].success);
    CHECK(g.acks[0].error == "OperatorStop");

    // A new command resumes and completes normally.
    g.server_command("cmd-open-2", sm::Event::CommandOpen, sm::State::Open);
    g.run_ticks(SimulatedGate::kTravelTicks);
    CHECK(g.machine.state() == sm::State::Open);
    REQUIRE(g.acks.size() == 2);
    CHECK(g.acks[1].command_id == "cmd-open-2");
    CHECK(g.acks[1].success);
}

TEST_CASE("unreachable command runs out the deadline as the last resort") {
    SimulatedGate g;
    g.boot_closed();
    // A close commanded while already Closed: the state machine drops
    // the event (no transition, no reason — the gate is already there,
    // but the tracker only fires on transitions), so only rule 5 can
    // end it.
    g.server_command("cmd-close", sm::Event::CommandClose, sm::State::Closed);

    g.run_ticks(static_cast<int>(SimulatedGate::kCommandDeadlineMs / 100) + 1);
    REQUIRE(g.acks.size() == 1);
    CHECK_FALSE(g.acks[0].success);
    CHECK(g.acks[0].error == "deadline exceeded awaiting terminal state");
}
