// v2_stack_test.cpp — in-process end to end: the firmware core, the
// virtual operator, and the real server dispatcher, joined by a modelled
// link. Message passing replaces gRPC; everything else is production code.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "auth/allowlist_store.hpp"
#include "dash/event_broadcaster.hpp"
#include "fusion/fusion_engine.hpp"
#include "gate_service.pb.h"
#include "gate_sim/virtual_gate.hpp"
#include "gate_sim/virtual_operator.hpp"
#include "gate_state_machine/gate_persist.hpp"
#include "gate_state_machine/rte_core.hpp"
#include "rpc/gate_dispatch.hpp"

namespace sm = gate::state_machine;
namespace v1 = gate::v1;
using gate::sim::PowerModel;
using gate::sim::VirtualOperator;
using Out = sm::RteAuthority::Output;

namespace {

struct RamFlash final : sm::FlashIo {
    std::vector<std::uint8_t> mem =
        std::vector<std::uint8_t>(sm::EventRing::kSectors * sm::EventRing::kSectorSize, 0xFF);
    void read(std::size_t o, std::uint8_t* out, std::size_t n) override {
        std::copy_n(mem.begin() + static_cast<std::ptrdiff_t>(o), n, out);
    }
    void program(std::size_t o, const std::uint8_t* d, std::size_t n) override {
        for (std::size_t i = 0; i < n; ++i) {
            mem[o + i] &= d[i];
        }
    }
    void erase_sector(std::size_t s) override {
        std::fill_n(mem.begin() + static_cast<std::ptrdiff_t>(s * sm::EventRing::kSectorSize),
                    sm::EventRing::kSectorSize, 0xFF);
    }
};

struct RamKv final : sm::KvStore {
    std::map<std::string, std::vector<std::uint8_t>> m;
    bool get(const char* k, void* out, std::size_t len) override {
        const auto it = m.find(k);
        if (it == m.end() || it->second.size() != len) {
            return false;
        }
        std::copy(it->second.begin(), it->second.end(), static_cast<std::uint8_t*>(out));
        return true;
    }
    bool set(const char* k, const void* d, std::size_t len) override {
        const auto* p = static_cast<const std::uint8_t*>(d);
        m[k].assign(p, p + len);
        return true;
    }
};

struct Locks final : sm::LockIo {
    VirtualOperator* op = nullptr;
    bool bolts = false;
    bool mags = true;
    bool permit = false;
    void drive(bool b, bool m) override {
        bolts = b;
        mags = m;
        update();
    }
    sm::LockFeedback feedback() override {
        sm::LockFeedback f;
        f.bolt_retracted = {bolts, bolts};
        f.bolt_extended = {!bolts, !bolts};
        f.mag_bonded = {mags, mags, false};
        return f;
    }
    void set_permit(bool on) override {
        permit = on;
        update();
    }
    bool permit_readback() override { return permit; }
    void update() { op->set_permit(permit && bolts && !mags); }  // the hardware AND
};

// One gate: state machine, supervisor, operator, RTE core.
struct Gate {
    VirtualOperator op{{}};
    Locks locks;
    sm::OperatorActuator actuator{op.frx, op.trg};
    sm::StateMachine machine{
        sm::Config{.lock_layout = sm::LockLayout::Four, .profile = sm::ActuatorProfile::Operator}};
    sm::GateSupervisor sup{{}, locks, actuator};
    RamFlash flash;
    sm::EventRing ring{flash};
    RamKv kv;
    sm::GatePersist persist{kv};
    sm::RteCore core{persist, &ring, 5'000};
    PowerModel power;
    std::uint64_t now = 1;
    bool link = true;
    bool was_closed = true;
    bool was_open = false;
    std::uint8_t next_id = 1;
    sm::ServedGuard::Id last_id{};

    Gate() {
        locks.op = &op;
        ring.mount();
        sup.apply(machine.init_closed(), now);
        sup.assume_engaged();
    }
    void post(sm::Event e, sm::OpenOrigin o = sm::OpenOrigin::Remote) {
        sup.observe(e, now);
        sup.apply(machine.handle(e, o), now);
    }
    void run(std::uint32_t ms, bool pressed = false) {
        for (std::uint32_t t = 0; t < ms; t += 20) {
            now += 20;
            op.tick(20);
            machine.set_battery(power.level());
            power.drain(op.current_ma() / 1000.0, 20);
            const auto ev = sup.tick(now, op.status_lit());
            for (std::size_t i = 0; i < ev.count; ++i) {
                post(ev.items[i]);
            }
            if (op.at_closed() != was_closed) {
                was_closed = op.at_closed();
                post(was_closed ? sm::Event::LimitClosedHit : sm::Event::LimitClosedReleased);
            }
            if (op.at_open() != was_open) {
                was_open = op.at_open();
                post(was_open ? sm::Event::LimitOpenHit : sm::Event::LimitOpenReleased);
            }
            sm::RteCore::Sample s;
            s.now_ms = now;
            s.pressed = pressed;
            s.link_up = link;
            s.wire_state = sm::wire_state(machine.state());
            s.fresh_id = {next_id, 0xAB};
            s.base.boot_id = 1;
            const auto step = core.tick(s);
            if (step.output == Out::OpenLocal) {
                last_id = step.id;
                post(sm::Event::CommandOpen, sm::OpenOrigin::Rte);
            }
            if (step.post_close) {
                post(sm::Event::CommandClose);
            }
        }
    }
};

struct Server {
    gate::auth::AllowlistStore store = gate::auth::AllowlistStore::open(":memory:");
    gate::fusion::FusionEngine fusion{store};
    gate::dash::EventBroadcaster bus;
    gate::rpc::GateDispatch d{bus, &store, &fusion, "s"};
    std::unique_ptr<gate::dash::EventBroadcaster::Subscription> sub = bus.subscribe({});
    std::vector<v1::GateCommand> commands() {
        std::vector<v1::GateCommand> out;
        for (const auto& e : sub->drain_now()) {
            if (e.has_command()) {
                out.push_back(e.command());
            }
        }
        return out;
    }
};

v1::EventBatch to_batch(const sm::EventRecord& r) {
    v1::EventBatch b;
    auto* e = b.add_events();
    e->set_seq(r.seq);
    e->set_boot_id(r.boot_id);
    e->set_kind(r.kind);
    e->set_request_id(std::string(r.request_id.begin(), r.request_id.end()));
    return b;
}

std::string text_id(const sm::ServedGuard::Id& id) {
    char t[37];
    sm::ServedGuard::format_uuid(id, t);
    return t;
}

}  // namespace

TEST_CASE("RTE offline: opens, closes, reports, frees the log", "[e2e]") {
    Gate g;
    Server s;
    g.link = false;
    g.run(200, true);
    g.run(3'000);
    CHECK((g.machine.state() == sm::State::Opening || g.machine.state() == sm::State::Open));
    g.run(1'500);
    CHECK(g.op.at_open());
    g.run(12'000);  // offline close, then locks
    CHECK(g.machine.state() == sm::State::Closed);
    CHECK(g.op.at_closed());

    const auto log = g.ring.pending(16);
    REQUIRE(log.size() == 1);
    const auto ack = s.d.ingest("g", to_batch(log[0]));
    g.ring.ack(static_cast<std::uint32_t>(ack.last_seq()));
    CHECK(g.ring.pending(16).empty());
    CHECK(s.d.ingest("g", to_batch(log[0])).last_seq() == log[0].seq);  // replay is harmless
}

TEST_CASE("ghost late grant: no second cycle, on either side", "[e2e]") {
    Gate g;
    Server s;
    g.link = false;
    g.run(200, true);
    g.run(20'000);
    REQUIRE(g.machine.state() == sm::State::Closed);
    const auto id = g.last_id;

    // Firmware refuses a grant that was already in flight.
    CHECK(g.core.admit_open(id, 0, 0, false) == sm::ServedGuard::Verdict::AlreadyServed);

    // So does the server once the log reaches it.
    s.d.ingest("g", to_batch(g.ring.pending(1).at(0)));
    gate::rpc::GateArbiter::Request r;
    r.request_id = text_id(id);
    r.side = v1::GATE_SIDE_EXIT;
    CHECK(s.d.submit("g", r).status() == v1::OpenReply::ALREADY_SERVED);
    CHECK(s.commands().empty());
}

TEST_CASE("served ids survive a controller reboot", "[e2e][persist]") {
    RamKv kv;
    sm::GatePersist p{kv};
    const sm::ServedGuard::Id id{9, 9};
    {
        sm::RteCore core{p, nullptr, 1'000};
        CHECK(core.admit_open(id, 0, 0, false) == sm::ServedGuard::Verdict::Accept);
    }
    sm::RteCore rebooted{p, nullptr, 1'000};
    CHECK(rebooted.admit_open(id, 0, 0, false) == sm::ServedGuard::Verdict::AlreadyServed);
}

TEST_CASE("persist: lockdown, boot id, config defaults and merge", "[persist]") {
    RamKv kv;
    sm::GatePersist p{kv};
    CHECK_FALSE(p.load_lockdown());
    p.save_lockdown(true);
    CHECK(p.load_lockdown());
    CHECK(p.next_boot_id() == 1);
    CHECK(p.next_boot_id() == 2);
    CHECK(p.load_config().offline_close_s == 30);

    sm::StoredConfig in;
    in.version = 4;
    in.offline_close_s = 0;
    sm::StoredConfig merged;
    REQUIRE(p.save_config(p.load_config(), in, merged));
    CHECK(p.load_config().version == 4);
    CHECK(p.load_config().offline_close_s == 30);

    kv.m["config"] = {1, 2, 3};  // an older layout
    CHECK(p.load_config().version == 0);
}

TEST_CASE("RTE core: expired commands, blocked close retried, link-up grant", "[e2e]") {
    Gate g;
    CHECK(g.core.admit_open({1}, 100, 200, true) == sm::ServedGuard::Verdict::Expired);
    g.link = false;
    g.run(200, true);
    g.run(4'800);
    REQUIRE(g.op.at_open());
    g.op.set_beam_blocked(true);
    g.post(sm::Event::SafetyBeamTripped);
    g.run(6'000);
    CHECK(g.op.at_open());  // refused
    g.op.set_beam_blocked(false);
    g.post(sm::Event::SafetyBeamCleared);
    g.run(20'000);
    CHECK(g.op.at_closed());
}

TEST_CASE("operator models: seized motor draws inrush; beam reverses a close", "[operator]") {
    VirtualOperator op{{}};
    op.set_permit(true);
    op.frx.set(true);
    op.tick(4'000);
    CHECK(op.at_open());
    op.frx.set(false);
    op.trg.set(true);
    op.trg.set(false);
    op.tick(1'000);
    CHECK(op.direction() < 0);
    CHECK(op.current_ma() == 6'000);
    op.set_beam_blocked(true);
    CHECK(op.direction() > 0);
    op.seize(true);
    CHECK(op.current_ma() == 14'000);
}

TEST_CASE("an open permit stops the operator at once", "[operator]") {
    VirtualOperator op{{}};
    op.frx.set(true);  // no permit: inhibited
    op.tick(100);
    CHECK(op.direction() == 0);
    op.frx.set(false);
    op.set_permit(true);
    op.frx.set(true);
    CHECK(op.direction() > 0);
    op.set_permit(false);
    CHECK(op.direction() == 0);
}

TEST_CASE("outage through the battery bands", "[power]") {
    Gate g;
    g.power.mains = false;
    g.power.volts = 23.5;
    g.run(40);
    g.post(sm::Event::CommandOpen);
    CHECK(g.machine.state() == sm::State::Closed);  // reserve refuses remote
    g.post(sm::Event::CommandOpen, sm::OpenOrigin::Rte);
    CHECK(g.machine.state() == sm::State::Unlocking);  // the exit does not
    g.power.volts = 21.0;
    g.run(1'000);
    CHECK(g.power.level() == sm::BatteryLevel::BelowCutoff);

    PowerModel p;
    p.mains = false;
    p.volts = 23.0;
    p.drain(10.0, 10'000);
    CHECK(p.critical());
    CHECK(p.level() == sm::BatteryLevel::BelowReserve);
}

TEST_CASE("legacy firmware: plain open, full cycle, no v2 commands", "[e2e][legacy]") {
    Server s;
    gate::sim::VirtualGate::Config c;
    c.travel_ms = 1'000;
    gate::sim::VirtualGate g(c);
    g.boot_closed();
    REQUIRE(g.capabilities() == 0);

    v1::Telemetry t;
    t.set_gate_id("g");
    t.set_capabilities(g.capabilities());
    t.set_gate_state(v1::GATE_STATE_CLOSED);
    t.set_safety_beam_clear(true);
    s.d.on_telemetry(t);

    gate::rpc::GateArbiter::Request r;
    r.request_id = "a";
    s.d.submit("g", r);
    const auto cmds = s.commands();
    REQUIRE(cmds.size() == 1);
    CHECK_FALSE(cmds[0].has_expires_ts());
    CHECK_FALSE(s.d.supports("g", v1::COMMAND_KIND_LOCKDOWN_ON));
    CHECK_FALSE(s.d.supports("g", v1::COMMAND_KIND_CONFIGURE));

    g.issue(cmds[0].command_id(), gate::sim::VirtualGate::Command::kOpen);
    for (int i = 0; i < 60; ++i) {
        g.advance(20);
    }
    CHECK(g.state() == sm::State::Open);
    const auto acks = g.drain_acks();
    REQUIRE(acks.size() == 1);
    CHECK(acks[0].success);
}
