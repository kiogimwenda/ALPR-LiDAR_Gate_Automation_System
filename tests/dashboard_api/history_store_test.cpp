// history_store_test.cpp — persistence and reporting behind the operator app.

#include "dash_api/history_store.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <string>

#include "dash_api/event_cache.hpp"

using Catch::Matchers::WithinAbs;
using gate::dash_api::DecisionQuery;
using gate::dash_api::HistoryStore;
using gate::dash_api::Role;
using gate::dash_api::UserRecord;

namespace {

// 2026-03-02 00:00:00 UTC (a Monday).
constexpr std::int64_t kDay = 1'772'409'600'000LL;
constexpr std::int64_t kHour = 3'600'000LL;

void set_ts(google::protobuf::Timestamp* ts, std::int64_t ms) {
    ts->set_seconds(ms / 1000);
    ts->set_nanos(static_cast<int>((ms % 1000) * 1'000'000));
}

gate::v1::DashboardEvent decision(const std::string& id, std::int64_t ms, const std::string& gate,
                                  const std::string& plate, gate::v1::AuthVerdict verdict,
                                  gate::v1::DenyReason reason = gate::v1::DENY_REASON_NONE,
                                  gate::v1::VehicleClass cls = gate::v1::VEHICLE_CLASS_SEDAN,
                                  float conf = 0.9f) {
    gate::v1::DashboardEvent ev;
    auto* d = ev.mutable_decision();
    d->set_decision_id(id);
    set_ts(d->mutable_decision_ts(), ms);
    d->set_gate_id(gate);
    d->set_matched_plate(plate);
    d->set_verdict(verdict);
    d->set_deny_reason(reason);
    d->set_matched_class(cls);
    d->set_combined_conf(conf);
    d->set_actor("auto");
    return ev;
}

DecisionQuery range(std::int64_t from, std::int64_t to, std::string plate = "") {
    DecisionQuery q;
    q.from_ms = from;
    q.to_ms = to;
    q.plate = std::move(plate);
    return q;
}

UserRecord user(std::string name, Role role, std::string hash) {
    UserRecord u;
    u.username = std::move(name);
    u.role = role;
    u.password_hash = std::move(hash);
    return u;
}

constexpr auto kOk = gate::v1::AUTH_VERDICT_AUTHORIZED;
constexpr auto kDenied = gate::v1::AUTH_VERDICT_DENIED;

std::unique_ptr<HistoryStore> store_with_gates() {
    auto h = HistoryStore::open(":memory:");
    std::string err;
    REQUIRE(h->configure_gate("in-1", "Main entry", "North", "entry", err));
    REQUIRE(h->configure_gate("out-1", "Main exit", "North", "exit", err));
    REQUIRE(h->configure_gate("side", "Side gate", "East", "both", err));
    return h;
}

}  // namespace

TEST_CASE("ingest is idempotent across stream replays", "[history]") {
    auto h = store_with_gates();
    const auto ev = decision("d1", kDay + kHour, "in-1", "kda 123a", kOk);
    h->ingest(ev);
    h->ingest(ev);  // reconnect replay
    const auto s = h->stats(kDay, kDay + 24 * kHour, 0);
    CHECK(s["captures"].asInt64() == 1);
    CHECK(s["entries"].asInt64() == 1);
    // Plates are stored normalised.
    CHECK(h->search(range(kDay, kDay + 24 * kHour, "KDA123A"))["total"].asInt64() == 1);
}

TEST_CASE("alerts are derived from blocklist hits, swapped plates and faults", "[history]") {
    auto h = store_with_gates();
    h->ingest(decision("b1", kDay, "in-1", "KBL666X", kDenied, gate::v1::DENY_REASON_BLOCKLISTED));
    h->ingest(decision("m1", kDay + 1000, "in-1", "KAA111A", gate::v1::AUTH_VERDICT_MANUAL_REVIEW,
                       gate::v1::DENY_REASON_CLASS_MISMATCH, gate::v1::VEHICLE_CLASS_TRUCK));
    h->ingest(decision("n1", kDay + 2000, "in-1", "KZZ999Z", kDenied,
                       gate::v1::DENY_REASON_NOT_ON_ALLOWLIST));  // no alert
    gate::v1::DashboardEvent f;
    f.mutable_fault()->set_fault_id("f1");
    f.mutable_fault()->set_gate_id("in-1");
    f.mutable_fault()->set_severity(gate::v1::FAULT_SEVERITY_ERROR);
    f.mutable_fault()->set_code("MOTOR_TIMEOUT");
    h->ingest(f);
    h->ingest(f);

    const auto a = h->alerts(true, 50, 0);
    REQUIRE(a["total"].asInt64() == 3);
    CHECK(h->unread_alerts() == 3);
    bool critical = false;
    for (const auto& it : a["items"]) {
        critical |=
            it["kind"].asString() == "blocklisted" && it["severity"].asString() == "critical";
    }
    CHECK(critical);

    CHECK(h->acknowledge_alert(a["items"][0]["id"].asInt64(), "guard1"));
    CHECK(h->unread_alerts() == 2);
    CHECK(h->acknowledge_all("guard1") == 2);
    CHECK(h->unread_alerts() == 0);
    CHECK_FALSE(h->acknowledge_alert(99'999, "guard1"));
}

TEST_CASE("a failed command raises an alert naming its gate", "[history]") {
    auto h = store_with_gates();
    gate::v1::DashboardEvent c;
    c.mutable_command()->set_command_id("c1");
    c.mutable_command()->set_gate_id("side");
    c.mutable_command()->set_kind(gate::v1::COMMAND_KIND_OPEN_GATE);
    h->ingest(c);
    gate::v1::DashboardEvent a;
    a.mutable_ack()->set_command_id("c1");
    a.mutable_ack()->set_completed(true);
    a.mutable_ack()->set_success(false);
    a.mutable_ack()->set_error_text("safety beam blocked");
    h->ingest(a);
    const auto alerts = h->alerts(false, 10, 0);
    REQUIRE(alerts["total"].asInt64() == 1);
    CHECK(alerts["items"][0]["gateId"].asString() == "side");
    CHECK(alerts["items"][0]["message"].asString().find("OPEN_GATE") != std::string::npos);
}

TEST_CASE("hourly buckets follow the client's time zone", "[history]") {
    auto h = store_with_gates();
    h->ingest(decision("h1", kDay + 6 * kHour, "in-1", "KAA001A", kOk));  // 06:00 UTC
    h->ingest(decision("h2", kDay + 6 * kHour + 5, "in-1", "KAA002A", kDenied));
    const auto utc = h->hourly(kDay, kDay + 24 * kHour, 0);
    CHECK(utc[6]["captures"].asInt64() == 2);
    const auto nairobi = h->hourly(kDay, kDay + 24 * kHour, 180);  // UTC+3
    CHECK(nairobi[9]["captures"].asInt64() == 2);
    CHECK(nairobi[9]["authorized"].asInt64() == 1);
    CHECK(nairobi[9]["denied"].asInt64() == 1);
    CHECK(h->stats(kDay, kDay + 24 * kHour, 180)["peakHour"].asInt() == 9);
}

TEST_CASE("sessions pair entries with exits and track who is on site", "[history]") {
    auto h = store_with_gates();
    h->ingest(decision("e1", kDay + 8 * kHour, "in-1", "KCA100A", kOk));
    h->ingest(decision("x1", kDay + 8 * kHour + 30 * 60'000, "out-1", "KCA100A", kOk));  // 30 min
    h->ingest(decision("e2", kDay + 9 * kHour, "in-1", "KCB200B", kOk));                 // still in
    h->ingest(decision("e3", kDay + 9 * kHour, "side", "KCC300C", kOk));  // 'both' gate: no session
    h->ingest(
        decision("e4", kDay + 9 * kHour + 5, "in-1", "KCD400D", kDenied));  // denied: no session

    const auto s = h->sessions(kDay, kDay + 12 * kHour);
    CHECK(s["completed"].asInt64() == 1);
    CHECK_THAT(s["avgMinutes"].asDouble(), WithinAbs(30.0, 0.01));
    REQUIRE(s["onSite"].size() == 1);
    CHECK(s["onSite"][0]["plate"].asString() == "KCB200B");
    CHECK(s["categories"][1]["count"].asInt64() == 1);  // 15–60 min

    const auto st = h->stats(kDay, kDay + 12 * kHour, 0);
    CHECK(st["currentlyOnSite"].asInt64() == 1);
    CHECK(st["entries"].asInt64() == 2);
    CHECK(st["exits"].asInt64() == 1);
}

TEST_CASE("search filters combine and page", "[history]") {
    auto h = store_with_gates();
    for (int i = 0; i < 30; ++i) {
        h->ingest(decision("s" + std::to_string(i), kDay + i * 60'000, i % 2 ? "in-1" : "out-1",
                           i < 10 ? "KDA" + std::to_string(100 + i) + "A" : "KXX999X",
                           i % 3 ? kOk : kDenied, gate::v1::DENY_REASON_NONE,
                           i % 5 ? gate::v1::VEHICLE_CLASS_SEDAN : gate::v1::VEHICLE_CLASS_SUV,
                           0.5f + static_cast<float>(i) / 100.0f));
    }
    DecisionQuery q = range(kDay, kDay + kHour);
    q.limit = 7;
    auto r = h->search(q);
    CHECK(r["total"].asInt64() == 30);
    CHECK(r["items"].size() == 7);
    CHECK(r["items"][0]["decisionId"].asString() == "s29");  // newest first

    q.plate = "kda-10";
    CHECK(h->search(q)["total"].asInt64() == 10);
    q.plate.clear();
    q.vehicle_class = "SUV";
    CHECK(h->search(q)["total"].asInt64() == 6);
    q.vehicle_class.clear();
    q.direction = "entry";
    CHECK(h->search(q)["total"].asInt64() == 15);
    q.direction.clear();
    q.min_confidence = 0.75f;
    CHECK(h->search(q)["total"].asInt64() == 5);
    q.min_confidence.reset();
    q.verdict = "DENIED";
    CHECK(h->search(q)["total"].asInt64() == 10);
}

TEST_CASE("CSV export escapes and defuses spreadsheet formulas", "[history]") {
    auto h = store_with_gates();
    auto ev =
        decision("csv1", kDay, "in-1", "KAA111A", kDenied, gate::v1::DENY_REASON_NOT_ON_ALLOWLIST);
    ev.mutable_decision()->set_reason_text("=HYPERLINK(\"x\"), plate \"unknown\"");
    h->ingest(ev);
    const auto csv = h->export_csv(range(kDay, kDay + kHour));
    CHECK(csv.rfind("time_utc,gate,direction,plate", 0) == 0);
    CHECK(csv.find("\"'=HYPERLINK(\"\"x\"\"), plate \"\"unknown\"\"\"") != std::string::npos);
    CHECK(csv.find("Main entry,entry,KAA111A,DENIED,NOT_ON_ALLOWLIST,SEDAN,0.900") !=
          std::string::npos);
}

TEST_CASE("rejected reads and verdict breakdowns", "[history]") {
    auto h = store_with_gates();
    h->ingest(decision("r1", kDay, "in-1", "", kDenied, gate::v1::DENY_REASON_NO_PLATE_FOUND));
    h->ingest(decision("r2", kDay + 1, "in-1", "KAB12", gate::v1::AUTH_VERDICT_LOW_CONFIDENCE,
                       gate::v1::DENY_REASON_LOW_ALPR_CONF, gate::v1::VEHICLE_CLASS_SEDAN, 0.3f));
    h->ingest(decision("r3", kDay + 2, "in-1", "KAB123C", kOk));
    const auto r = h->rejected(kDay, kDay + kHour, 25, 0);
    CHECK(r["total"].asInt64() == 2);
    CHECK(r["breakdown"].size() == 2);
    const auto v = h->verdicts(kDay, kDay + kHour);
    CHECK(v["byVerdict"].size() == 3);
    CHECK(h->stats(kDay, kDay + kHour, 0)["uniqueVehicles"].asInt64() == 2);  // empty plate ignored
}

TEST_CASE("top plates and plate history", "[history]") {
    auto h = store_with_gates();
    for (int i = 0; i < 5; ++i) {
        h->ingest(decision("t" + std::to_string(i), kDay + i * kHour, "in-1", "KRE777R", kOk));
    }
    h->ingest(decision("u1", kDay, "in-1", "KOT111T", kDenied));
    const auto top = h->top_plates(kDay, kDay + 24 * kHour, 5);
    REQUIRE(top.size() == 2);
    CHECK(top[0]["plate"].asString() == "KRE777R");
    CHECK(top[0]["visits"].asInt64() == 5);
    const auto p = h->plate_history("kre 777r", 3);
    CHECK(p["visits"].asInt64() == 5);
    CHECK(p["history"].size() == 3);
}

TEST_CASE("users: roles, unique names, and never zero admins", "[history][users]") {
    auto h = HistoryStore::open(":memory:");
    UserRecord admin = user("admin", Role::kAdmin, "pbkdf2-sha256$1$aa$bb");
    std::string err;
    REQUIRE(h->save_user(admin, err));
    UserRecord op = user("guard1", Role::kOperator, "pbkdf2-sha256$1$aa$bb");
    REQUIRE(h->save_user(op, err));
    UserRecord dup = user("GUARD1", Role::kViewer, "x");
    CHECK_FALSE(h->save_user(dup, err));  // usernames are case-insensitive
    UserRecord bad = user("no spaces", Role::kViewer, "x");
    CHECK_FALSE(h->save_user(bad, err));

    // Demoting or disabling the last admin is refused.
    admin.role = Role::kOperator;
    admin.password_hash.clear();
    CHECK_FALSE(h->save_user(admin, err));
    CHECK(err.find("last active admin") != std::string::npos);
    CHECK_FALSE(h->delete_user(admin.id, err));

    // With a second admin it is allowed — and an empty hash keeps the old one.
    op.role = Role::kAdmin;
    op.password_hash.clear();
    REQUIRE(h->save_user(op, err));
    CHECK(h->user_by_id(op.id)->password_hash == "pbkdf2-sha256$1$aa$bb");
    CHECK(h->save_user(admin, err));
    CHECK(h->user_by_name("ADMIN")->role == Role::kOperator);
    CHECK(h->delete_user(admin.id, err));
    CHECK(h->user_count() == 1);
}

TEST_CASE("gate configuration is validated", "[history]") {
    auto h = HistoryStore::open(":memory:");
    std::string err;
    CHECK_FALSE(h->configure_gate("g", "", "", "sideways", err));
    CHECK_FALSE(h->configure_gate("", "", "", "entry", err));
    CHECK(h->configure_gate("g", "Gate", "", "entry", err));
    const auto g = h->gates();
    REQUIRE(g.size() == 1);
    CHECK(g[0]["direction"].asString() == "entry");
    CHECK_FALSE(g[0]["online"].asBool());
}

TEST_CASE("retention purge keeps recent rows and unread alerts", "[history]") {
    auto h = store_with_gates();
    h->ingest(decision("old", kDay, "in-1", "KAA111A", kDenied, gate::v1::DENY_REASON_BLOCKLISTED));
    h->ingest(decision("new", kDay + 40LL * 24 * kHour, "in-1", "KAA111A", kOk));
    const auto removed = h->purge(30, kDay + 41LL * 24 * kHour);
    CHECK(removed == 1);  // the old decision; its alert is unread, so kept
    CHECK(h->search(range(0, kDay + 50LL * 24 * kHour))["total"].asInt64() == 1);
    CHECK(h->unread_alerts() == 1);
}

TEST_CASE("event cache follows the cursor down after a server restart", "[history][cache]") {
    gate::dash_api::EventCache cache;
    gate::v1::DashboardEvent ev;
    ev.set_event_id(5000);
    ev.mutable_telemetry()->set_gate_id("g");
    cache.ingest(ev);
    CHECK(cache.last_event_id() == 5000);
    ev.set_event_id(4999);  // within one stream a lower id is a stray: ignored
    cache.ingest(ev);
    CHECK(cache.last_event_id() == 5000);
    cache.begin_stream();  // reconnect …
    ev.set_event_id(3);    // … to a restarted server counting from 1 again
    cache.ingest(ev);
    CHECK(cache.last_event_id() == 3);

    int seen = 0;
    cache.add_listener([&](const gate::v1::DashboardEvent&) { ++seen; });
    cache.ingest(ev);
    CHECK(seen == 1);
}
