// gate_dispatch_test.cpp — sole publisher, capabilities, passes, ingest,
// fusion order and the auth DB migration.

#include "rpc/gate_dispatch.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <sqlite3.h>
#include <vector>

#include "auth/allowlist_store.hpp"
#include "fusion/fusion_engine.hpp"
#include "rpc/admin_service.hpp"
#include "rpc/dashboard_service.hpp"

namespace v1 = gate::v1;
using gate::rpc::GateArbiter;
using gate::rpc::GateDispatch;

namespace {

struct Rig {
    gate::auth::AllowlistStore store = gate::auth::AllowlistStore::open(":memory:");
    gate::fusion::FusionEngine fusion{store};
    gate::dash::EventBroadcaster bus;
    GateDispatch d{bus, &store, &fusion, "s"};
    std::unique_ptr<gate::dash::EventBroadcaster::Subscription> sub = bus.subscribe({});

    Rig() { d.set_clock_ms(1'000); }
    std::vector<v1::DashboardEvent> events() { return sub->drain_now(); }
    std::vector<v1::GateCommand> commands() {
        std::vector<v1::GateCommand> out;
        for (const auto& e : events()) {
            if (e.has_command()) {
                out.push_back(e.command());
            }
        }
        return out;
    }
    v1::AuthorizeRequest frame(const std::string& plate, v1::GateSide side = v1::GATE_SIDE_ENTRY) {
        v1::AuthorizeRequest r;
        r.set_site_id("s");
        r.set_actor("auto");
        r.mutable_frame()->set_gate_id("g");
        r.mutable_frame()->set_side(side);
        auto* p = r.mutable_frame()->add_plates();
        p->set_plate_text(plate);
        p->set_detection_conf(0.95F);
        p->set_ocr_conf(0.95F);
        auto* veh = r.mutable_frame()->add_vehicles();
        veh->set_vehicle_class(v1::VEHICLE_CLASS_SEDAN);
        veh->set_class_conf(0.95F);
        return r;
    }
    void telemetry(std::uint32_t caps, v1::GateState st = v1::GATE_STATE_OPEN, bool beam = true) {
        v1::Telemetry t;
        t.set_gate_id("g");
        t.set_capabilities(caps);
        t.set_gate_state(st);
        t.set_safety_beam_clear(beam);
        d.on_telemetry(t);
    }
};

GateArbiter::Request rte(const std::string& id) {
    GateArbiter::Request r;
    r.request_id = id;
    r.side = v1::GATE_SIDE_EXIT;
    r.source = v1::OPEN_SOURCE_RTE;
    return r;
}

}  // namespace

TEST_CASE("grants are published once with tags and audited", "[dispatch]") {
    Rig r;
    r.telemetry(1);
    r.d.submit("g", rte("a"));
    r.d.submit("g", rte("a"));
    const auto c = r.commands();
    REQUIRE(c.size() == 1);
    CHECK(c[0].source() == v1::OPEN_SOURCE_RTE);
    CHECK(c[0].side() == v1::GATE_SIDE_EXIT);
    CHECK(c[0].request_id() == "a");
    CHECK(c[0].has_expires_ts());
    CHECK(r.store.open_audit_count("g") == 1);
}

TEST_CASE("legacy firmware gets plain opens and no v2 commands", "[dispatch]") {
    Rig r;
    r.telemetry(0);
    r.d.submit("g", rte("a"));
    const auto c = r.commands();
    REQUIRE(c.size() == 1);
    CHECK_FALSE(c[0].has_expires_ts());
    CHECK_FALSE(r.d.supports("g", v1::COMMAND_KIND_LOCKDOWN_ON));
    CHECK(r.d.supports("g", v1::COMMAND_KIND_OPEN_GATE));
    r.telemetry(1);
    CHECK(r.d.supports("g", v1::COMMAND_KIND_LOCKDOWN_ON));
    CHECK_FALSE(r.d.supports("g", v1::COMMAND_KIND_CALIBRATE));
}

TEST_CASE("a beam cycle completes the grant, next waiter goes", "[dispatch]") {
    Rig r;
    r.d.submit("g", rte("a"));
    CHECK(r.d.submit("g", rte("b")).status() == v1::OpenReply::QUEUED);
    CHECK(r.d.hold_auto_close("g"));
    r.telemetry(0, v1::GATE_STATE_OPEN, false);
    r.telemetry(0, v1::GATE_STATE_OPEN, true);
    CHECK(r.commands().size() == 2);
}

TEST_CASE("event ingest is idempotent and cancels a late grant", "[dispatch]") {
    Rig r;
    v1::EventBatch b;
    auto* e = b.add_events();
    e->set_boot_id(3);
    e->set_seq(9);
    e->set_kind(GateDispatch::kEventOfflineOpen);
    e->set_request_id(std::string(16, '\x11'));
    CHECK(r.d.ingest("g", b).last_seq() == 9);
    CHECK(r.d.ingest("g", b).boot_id() == 3);  // replay still acks
    CHECK_FALSE(r.store.controller_event_insert("g", b.events(0)));
    const std::string id = "11111111-1111-1111-1111-111111111111";
    CHECK(r.d.submit("g", rte(id)).status() == v1::OpenReply::ALREADY_SERVED);
    CHECK(r.commands().empty());
}

TEST_CASE("sticky force-open never beats the blocklist; admin force does", "[fusion]") {
    Rig r;
    r.store.blocklist_upsert("s", "KBAD", "theft", "admin");
    r.fusion.set_override("g", gate::fusion::OverrideState::kForceOpen);
    CHECK(r.fusion.decide(r.frame("KBAD")).deny_reason() == v1::DENY_REASON_BLOCKLISTED);

    auto q = r.frame("KBAD");
    q.set_force_reason("police escort");
    q.set_actor("admin:ian");
    const auto d = r.fusion.decide(q);
    CHECK(d.verdict() == v1::AUTH_VERDICT_AUTHORIZED);
    REQUIRE(d.alerts_size() == 1);
    r.d.publish_alerts(d);
    bool critical = false;
    for (const auto& e : r.events()) {
        critical =
            critical || (e.has_alert() && e.alert().severity() == v1::FAULT_SEVERITY_CRITICAL);
    }
    CHECK(critical);
}

TEST_CASE("low-confidence block read alerts without changing the verdict", "[fusion]") {
    Rig r;
    r.store.blocklist_upsert("s", "KBAD", "theft");
    auto q = r.frame("KBAD");
    q.mutable_frame()->mutable_plates(0)->set_ocr_conf(0.1F);
    q.mutable_frame()->mutable_plates(0)->set_detection_conf(0.1F);
    q.mutable_frame()->mutable_vehicles(0)->set_class_conf(0.1F);
    const auto d = r.fusion.decide(q);
    CHECK(d.verdict() == v1::AUTH_VERDICT_LOW_CONFIDENCE);
    CHECK(d.alerts(0) == "BLOCKLIST_LOW_CONFIDENCE");
}

TEST_CASE("single-visit pass burns on confirmed open only", "[guest]") {
    Rig r;
    v1::GuestPass p;
    p.set_pass_id("p");
    p.set_site_id("s");
    p.set_plate_text("KGST");
    p.set_kind(v1::GuestPass::SINGLE_VISIT);
    p.add_gate_ids("g");
    r.store.guest_pass_create(p, std::time(nullptr) - 10);

    auto d = r.fusion.decide(r.frame("KGST"));
    REQUIRE(d.verdict() == v1::AUTH_VERDICT_AUTHORIZED);
    CHECK(d.pass_id() == "p");
    CHECK(d.alerts(0) == "GUEST_ARRIVED");
    r.d.submit_decision(d, v1::OPEN_SOURCE_GUEST);
    const auto c = r.commands();
    REQUIRE(c.size() == 1);
    CHECK(c[0].source() == v1::OPEN_SOURCE_GUEST);

    v1::CommandAck failed;
    failed.set_command_id("other");
    failed.set_completed(true);
    r.d.on_ack(failed);
    CHECK(r.fusion.decide(r.frame("KGST")).verdict() == v1::AUTH_VERDICT_AUTHORIZED);

    v1::CommandAck ok;
    ok.set_command_id(c[0].command_id());
    ok.set_completed(true);
    ok.set_success(true);
    r.d.on_ack(ok);
    CHECK(r.fusion.decide(r.frame("KGST")).deny_reason() == v1::DENY_REASON_NOT_ON_ALLOWLIST);
    // The guest can still leave.
    CHECK(r.fusion.decide(r.frame("KGST", v1::GATE_SIDE_EXIT)).verdict() ==
          v1::AUTH_VERDICT_AUTHORIZED);
}

TEST_CASE("two cars cannot both claim a single visit; a failed open frees it", "[guest]") {
    Rig r;
    v1::GuestPass p;
    p.set_pass_id("p");
    p.set_site_id("s");
    p.set_plate_text("KGST");
    p.set_kind(v1::GuestPass::SINGLE_VISIT);
    r.store.guest_pass_create(p, std::time(nullptr) - 10);
    const auto now = std::time(nullptr);

    CHECK(r.store.guest_pass_reserve("p", "c1", now));
    CHECK_FALSE(r.store.guest_pass_reserve("p", "c2", now));
    r.store.guest_pass_settle("c1", false);
    CHECK(r.store.guest_pass_reserve("p", "c3", now));
    // A stale claim lapses.
    r.store.guest_pass_settle("c3", false);
    CHECK(r.store.guest_pass_reserve("p", "c4", now));
    CHECK(r.store.guest_pass_reserve("p", "c5",
                                     now + gate::auth::AllowlistStore::kReservationTtlS + 1));

    // Double ack and a settle after revocation change nothing.
    r.store.guest_pass_settle("c5", true);
    r.store.guest_pass_settle("c5", true);
    CHECK(r.store.guest_pass_list("s")[0].entries_used() == 1);
    CHECK(r.store.guest_pass_list("s")[0].status() == "used");
    CHECK_FALSE(r.store.guest_pass_reserve("p", "c6", now));

    auto d = r.fusion.decide(r.frame("KGST", v1::GATE_SIDE_EXIT));
    CHECK(r.d.submit_decision(d, v1::OPEN_SOURCE_GUEST).status() != v1::OpenReply::DENIED);
}

TEST_CASE("a claim survives a server restart", "[guest]") {
    const auto path = std::filesystem::temp_directory_path() / "gate_claim_test.db";
    std::filesystem::remove(path);
    const auto now = std::time(nullptr);
    {
        auto s = gate::auth::AllowlistStore::open(path);
        v1::GuestPass p;
        p.set_pass_id("p");
        p.set_site_id("s");
        p.set_plate_text("KGST");
        p.set_kind(v1::GuestPass::SINGLE_VISIT);
        s.guest_pass_create(p, now - 10);
        REQUIRE(s.guest_pass_reserve("p", "c1", now));
    }
    auto s = gate::auth::AllowlistStore::open(path);
    CHECK_FALSE(s.guest_pass_reserve("p", "c2", now));
    s.guest_pass_settle("c1", true);
    CHECK(s.guest_pass_list("s")[0].status() == "used");
    std::filesystem::remove(path);
}

TEST_CASE("locked gates refuse bypass commands; force and sides", "[dispatch]") {
    Rig r;
    gate::rpc::DashboardServiceImpl svc{r.bus, r.fusion, "s"};
    svc.use_dispatch(&r.d);
    r.telemetry(1);
    grpc::ServerContext ctx;
    v1::CommandAck ack;
    for (const auto kind : {v1::COMMAND_KIND_LATCH_OPEN, v1::COMMAND_KIND_PULSE_RELAY}) {
        v1::GateCommand c;
        c.set_gate_id("g");
        c.set_kind(kind);
        CHECK(svc.IssueCommand(&ctx, &c, &ack).error_code() ==
              grpc::StatusCode::FAILED_PRECONDITION);
    }
    CHECK(r.fusion.override_for("g") == gate::fusion::OverrideState::kNone);

    auto q = r.frame("KBAD");
    q.set_force_reason("x");
    q.set_actor("admin:mallory");
    v1::AuthDecision d;
    CHECK(svc.Authorize(&ctx, &q, &d).error_code() == grpc::StatusCode::PERMISSION_DENIED);

    // An unlabelled app open queues as an entry: a waiting exit goes first.
    r.d.submit("g", rte("first"));
    v1::GateCommand app;
    app.set_gate_id("g");
    app.set_kind(v1::COMMAND_KIND_OPEN_GATE);
    app.set_request_id("app");
    REQUIRE(svc.IssueCommand(&ctx, &app, &ack).ok());
    r.d.submit("g", rte("second"));
    (void)r.commands();
    r.telemetry(1, v1::GATE_STATE_OPEN, false);
    r.telemetry(1, v1::GATE_STATE_OPEN, true);
    const auto next = r.commands();
    REQUIRE(next.size() == 1);
    CHECK(next[0].request_id() == "second");
}

TEST_CASE("SetGateConfig needs a closed, capable gate and bumps the version", "[config]") {
    Rig r;
    gate::rpc::AdminServiceImpl svc{r.store};
    svc.use_dispatch(&r.d);
    grpc::ServerContext ctx;
    v1::SetGateConfigRequest q;
    q.set_gate_id("g");
    q.mutable_config()->set_auto_close_s(30);
    q.set_updated_by("ian");
    v1::GateConfigRecord out;

    r.telemetry(4, v1::GATE_STATE_OPEN);
    CHECK(svc.SetGateConfig(&ctx, &q, &out).error_code() == grpc::StatusCode::FAILED_PRECONDITION);
    r.telemetry(0, v1::GATE_STATE_CLOSED);
    CHECK(svc.SetGateConfig(&ctx, &q, &out).error_code() ==
          grpc::StatusCode::FAILED_PRECONDITION);  // legacy firmware
    r.telemetry(4, v1::GATE_STATE_CLOSED);
    REQUIRE(svc.SetGateConfig(&ctx, &q, &out).ok());
    CHECK(out.config().version() == 1);
    REQUIRE(svc.SetGateConfig(&ctx, &q, &out).ok());
    CHECK(out.config().version() == 2);
    const auto cmds = r.commands();
    REQUIRE(cmds.size() == 2);
    CHECK(cmds[1].kind() == v1::COMMAND_KIND_CONFIGURE);
    v1::GetGateConfigRequest g;
    g.set_gate_id("g");
    REQUIRE(svc.GetGateConfig(&ctx, &g, &out).ok());
    CHECK(out.config().auto_close_s() == 30);
    CHECK(out.updated_by() == "ian");
}

TEST_CASE("config bounds, unverified block and catch-up", "[config]") {
    Rig r;
    gate::rpc::AdminServiceImpl svc{r.store};
    svc.use_dispatch(&r.d);
    grpc::ServerContext ctx;
    r.telemetry(4, v1::GATE_STATE_CLOSED);
    v1::GateConfigRecord out;
    v1::SetGateConfigRequest q;
    q.set_gate_id("g");
    q.set_updated_by("ian");
    q.mutable_config()->set_bolt_hold_open_s(61);
    CHECK(svc.SetGateConfig(&ctx, &q, &out).error_code() == grpc::StatusCode::INVALID_ARGUMENT);
    q.mutable_config()->set_bolt_hold_open_s(45);
    q.clear_updated_by();
    CHECK(svc.SetGateConfig(&ctx, &q, &out).error_code() == grpc::StatusCode::INVALID_ARGUMENT);
    q.set_updated_by("ian");

    // Timers alone stay verified and keep automatic opens.
    REQUIRE(svc.SetGateConfig(&ctx, &q, &out).ok());
    CHECK(out.status() == v1::CONFIG_VERIFIED);

    // A type change blocks them.
    q.mutable_config()->set_type(v1::GATE_TYPE_DUAL_LEAF);
    REQUIRE(svc.SetGateConfig(&ctx, &q, &out).ok());
    CHECK(out.status() == v1::CONFIG_UNVERIFIED);
    v1::AuthDecision d;
    d.set_decision_id("d");
    d.set_gate_id("g");
    (void)r.commands();
    CHECK(r.d.submit_decision(d, v1::OPEN_SOURCE_PLATE).status() == v1::OpenReply::DENIED);
    CHECK(r.commands().empty());
    CHECK(r.d.submit("g", rte("x")).status() == v1::OpenReply::GRANTED);  // the exit still works

    // A gate behind on version gets the config once it reports closed.
    (void)r.commands();
    r.telemetry(4, v1::GATE_STATE_CLOSED);
    const auto c = r.commands();
    REQUIRE(c.size() == 1);
    CHECK(c[0].kind() == v1::COMMAND_KIND_CONFIGURE);
    r.telemetry(4, v1::GATE_STATE_CLOSED);
    CHECK(r.commands().empty());  // throttled
}

TEST_CASE("pass respects gate, expiry and revocation", "[guest]") {
    Rig r;
    const auto now = std::time(nullptr);
    v1::GuestPass p;
    p.set_pass_id("p");
    p.set_site_id("s");
    p.set_plate_text("KGST");
    p.set_kind(v1::GuestPass::UNTIL);
    p.mutable_valid_until()->set_seconds(now + 100);
    p.add_gate_ids("g");
    r.store.guest_pass_create(p, now);
    CHECK(r.store.guest_pass_find("s", "KGST", "g", v1::GATE_SIDE_ENTRY, now + 1));
    CHECK_FALSE(r.store.guest_pass_find("s", "KGST", "other", v1::GATE_SIDE_ENTRY, now + 1));
    CHECK_FALSE(r.store.guest_pass_find("s", "KGST", "g", v1::GATE_SIDE_ENTRY, now + 100));
    r.store.guest_pass_revoke("p");
    CHECK_FALSE(r.store.guest_pass_find("s", "KGST", "g", v1::GATE_SIDE_ENTRY, now + 1));
}

namespace {

// The v1 schema as shipped, with rows.
void make_v1(const std::filesystem::path& path, const char* extra = "") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    const std::string sql = std::string{R"sql(
        CREATE TABLE schema_version(version INTEGER NOT NULL);
        INSERT INTO schema_version VALUES(1);
        CREATE TABLE allowlist (
            site_id TEXT NOT NULL, plate_text TEXT NOT NULL,
            owner_name TEXT NOT NULL DEFAULT '', owner_unit TEXT NOT NULL DEFAULT '',
            valid_from INTEGER NOT NULL DEFAULT 0, valid_until INTEGER NOT NULL DEFAULT 0,
            notes TEXT NOT NULL DEFAULT '', added_by TEXT NOT NULL DEFAULT '',
            added_ts INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (site_id, plate_text)) WITHOUT ROWID;
        INSERT INTO allowlist(site_id, plate_text, owner_name) VALUES('s','KFAM','Ian');
        CREATE TABLE blocklist(site_id TEXT NOT NULL, plate_text TEXT NOT NULL,
            reason TEXT NOT NULL DEFAULT '', added_ts INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY(site_id, plate_text)) WITHOUT ROWID;
        INSERT INTO blocklist VALUES('s','KOLD','r',5);
    )sql"} + extra;
    REQUIRE(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}

int version_of(const std::filesystem::path& path) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db, "SELECT version FROM schema_version", -1, &s, nullptr);
    sqlite3_step(s);
    const int v = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    sqlite3_close(db);
    return v;
}

}  // namespace

TEST_CASE("auth DB migrates a copied v1 DB, keeps rows, backs up first", "[migration]") {
    const auto dir = std::filesystem::temp_directory_path();
    const auto v1_path = dir / "gate_auth_v1.db";
    const auto copy = dir / "gate_auth_v1_copy.db";
    const auto backup = copy.string() + ".v1.bak";
    for (const auto& p : {v1_path, copy, std::filesystem::path{backup}}) {
        std::filesystem::remove(p);
    }
    make_v1(v1_path);
    std::filesystem::copy_file(v1_path, copy);
    {
        auto store = gate::auth::AllowlistStore::open(copy);
        CHECK(store.is_blocklisted("s", "KOLD"));
        CHECK(store.blocklist_list("s", 10, "").entries.at(0).reason() == "r");
        CHECK(store.lookup("s", "KFAM")->owner_name() == "Ian");
    }
    CHECK(version_of(copy) == 3);
    CHECK(std::filesystem::exists(backup));
    CHECK(version_of(backup) == 1);
    CHECK(version_of(v1_path) == 1);
    auto again = gate::auth::AllowlistStore::open(copy);  // idempotent
    CHECK(again.is_blocklisted("s", "KOLD"));
    for (const auto& p : {v1_path, copy, std::filesystem::path{backup}}) {
        std::filesystem::remove(p);
    }
}

TEST_CASE("a failed migration rolls back to v1", "[migration]") {
    const auto path = std::filesystem::temp_directory_path() / "gate_auth_bad.db";
    const auto backup = path.string() + ".v1.bak";
    std::filesystem::remove(path);
    std::filesystem::remove(backup);
    make_v1(path, "CREATE TABLE open_audit(x INTEGER);");  // collides late in the upgrade
    CHECK_THROWS_AS(gate::auth::AllowlistStore::open(path), gate::auth::AuthDbException);
    CHECK(version_of(path) == 1);
    CHECK(std::filesystem::exists(backup));
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "SELECT added_by FROM blocklist", nullptr, nullptr, nullptr) !=
          SQLITE_OK);
    sqlite3_close(db);
    std::filesystem::remove(path);
    std::filesystem::remove(backup);
}
