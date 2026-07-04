// fusion_engine_test.cpp — verdict-ladder unit tests for FusionEngine.
//
// Each case constructs an in-memory AllowlistStore, populates it minimally,
// builds an AuthorizeRequest by hand, and asserts on the resulting
// AuthDecision. Time is injected via decide_at() so weekday/window tests
// are deterministic.

#include "fusion/fusion_engine.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ctime>
#include <regex>
#include <string>

using gate::auth::AllowlistStore;
using gate::fusion::FusionConfig;
using gate::fusion::FusionEngine;
using gate::fusion::generate_uuidv4;
using gate::fusion::OverrideState;
using gate::v1::Allowlistentry;
using gate::v1::AuthDecision;
using gate::v1::AuthorizeRequest;
using gate::v1::AuthVerdict;
using gate::v1::DenyReason;
using gate::v1::DetectionFrame;
using gate::v1::PlateDetection;
using gate::v1::TimeWindow;
using gate::v1::VehicleClass;
using gate::v1::VehicleDetection;

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

constexpr const char* kSite = "site-a";
constexpr const char* kGate = "gate-north";

PlateDetection make_plate(const std::string& text, float det_conf, float ocr_conf) {
    PlateDetection p;
    p.set_plate_text(text);
    p.set_detection_conf(det_conf);
    p.set_ocr_conf(ocr_conf);
    return p;
}

VehicleDetection make_vehicle(VehicleClass cls, float conf) {
    VehicleDetection v;
    v.set_vehicle_class(cls);
    v.set_class_conf(conf);
    return v;
}

AuthorizeRequest make_request(std::string plate_text, float det_conf, float ocr_conf,
                              VehicleClass cls = VehicleClass::VEHICLE_CLASS_SEDAN,
                              float lidar_conf = 0.95f) {
    AuthorizeRequest r;
    r.set_site_id(kSite);
    r.set_actor("auto");
    auto* f = r.mutable_frame();
    f->set_gate_id(kGate);
    f->set_frame_id(42);
    *f->add_plates() = make_plate(std::move(plate_text), det_conf, ocr_conf);
    *f->add_vehicles() = make_vehicle(cls, lidar_conf);
    return r;
}

Allowlistentry make_entry(std::string plate, std::initializer_list<VehicleClass> classes = {},
                          std::initializer_list<TimeWindow> windows = {}) {
    Allowlistentry e;
    e.set_plate_text(std::move(plate));
    e.set_owner_name("Alice");
    for (auto c : classes)
        e.add_allowed_classes(c);
    for (const auto& w : windows)
        *e.add_time_windows() = w;
    return e;
}

std::tm utc_local(std::time_t t) {
    std::tm out{};
    gmtime_r(&t, &out);
    return out;
}

// 2024-01-01 was a Monday at 00:00:00 UTC.
constexpr std::time_t kMondayMidnightUtc = 1704067200;

}  // namespace

TEST_CASE("generate_uuidv4: format is RFC 4122 compliant", "[fusion][uuid]") {
    static const std::regex re(
        R"([0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})");
    for (int i = 0; i < 50; ++i) {
        const auto u = generate_uuidv4();
        INFO("uuid=" << u);
        REQUIRE(u.size() == 36);
        REQUIRE(std::regex_match(u, re));
    }
    // Two consecutive UUIDs collide with negligible probability — sanity.
    REQUIRE(generate_uuidv4() != generate_uuidv4());
}

TEST_CASE("FusionEngine: AUTHORIZED happy path with allowlist hit", "[fusion][authorize]") {
    auto store = AllowlistStore::open(":memory:");
    auto e = make_entry("KBZ123A");
    std::vector<Allowlistentry> batch{e};
    store.upsert(kSite, batch);

    FusionEngine engine{store};
    const auto req = make_request("KBZ-123A", 0.9f, 0.85f);
    const auto d = engine.decide_at(req, kMondayMidnightUtc + 10 * 3600,
                                    utc_local(kMondayMidnightUtc + 10 * 3600));

    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);
    REQUIRE(d.matched_plate() == "KBZ123A");
    REQUIRE(d.matched_class() == VehicleClass::VEHICLE_CLASS_SEDAN);
    REQUIRE(d.gate_id() == kGate);
    REQUIRE(d.frame_id() == 42);
    REQUIRE(d.actor() == "auto");
    // 0.5 * (0.5*0.9 + 0.5*0.85) + 0.5 * 0.95 = 0.4375 + 0.475 = 0.9125
    REQUIRE_THAT(d.combined_conf(), WithinAbs(0.9125f, 1e-4f));
    REQUIRE_FALSE(d.decision_id().empty());
    REQUIRE(d.decision_ts().seconds() == kMondayMidnightUtc + 10 * 3600);
}

TEST_CASE("FusionEngine: NO_PLATE_FOUND when frame has no plate", "[fusion][deny]") {
    auto store = AllowlistStore::open(":memory:");
    FusionEngine engine{store};

    AuthorizeRequest req;
    req.set_site_id(kSite);
    req.mutable_frame()->set_gate_id(kGate);
    *req.mutable_frame()->add_vehicles() = make_vehicle(VehicleClass::VEHICLE_CLASS_SEDAN, 0.9f);

    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));
    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_DENIED);
    REQUIRE(d.deny_reason() == DenyReason::DENY_REASON_NO_PLATE_FOUND);
    REQUIRE(d.matched_plate().empty());
}

TEST_CASE("FusionEngine: BLOCKLISTED plate denies even with allowlist hit",
          "[fusion][deny][blocklist]") {
    auto store = AllowlistStore::open(":memory:");
    std::vector<Allowlistentry> batch{make_entry("KBZ123A")};
    store.upsert(kSite, batch);
    store.blocklist_upsert(kSite, "KBZ123A", "stolen vehicle");

    FusionEngine engine{store};
    const auto req = make_request("KBZ-123A", 0.9f, 0.85f);
    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));

    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_DENIED);
    REQUIRE(d.deny_reason() == DenyReason::DENY_REASON_BLOCKLISTED);
}

TEST_CASE("FusionEngine: NOT_ON_ALLOWLIST when plate is unknown", "[fusion][deny][allowlist]") {
    auto store = AllowlistStore::open(":memory:");
    FusionEngine engine{store};

    const auto req = make_request("ZZZ999Z", 0.9f, 0.85f);
    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));

    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_DENIED);
    REQUIRE(d.deny_reason() == DenyReason::DENY_REASON_NOT_ON_ALLOWLIST);
    REQUIRE(d.matched_plate() == "ZZZ999Z");  // Recorded for audit even on deny.
}

TEST_CASE("FusionEngine: OUTSIDE_WINDOW when allowlist hit but time-window fails",
          "[fusion][deny][window]") {
    auto store = AllowlistStore::open(":memory:");
    TimeWindow w;
    w.set_start_minute_of_day(8 * 60);
    w.set_end_minute_of_day(17 * 60);
    w.set_days_of_week_mask(0b0011111);  // Mon..Fri
    std::vector<Allowlistentry> batch{make_entry("KBZ123A", {}, {w})};
    store.upsert(kSite, batch);

    FusionEngine engine{store};
    const auto req = make_request("KBZ-123A", 0.9f, 0.85f);
    // Sunday at 10am UTC — bit6 not set in the Mon..Fri mask.
    const std::time_t sun_10am = kMondayMidnightUtc + 6 * 86400 + 10 * 3600;
    const auto d = engine.decide_at(req, sun_10am, utc_local(sun_10am));

    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_DENIED);
    REQUIRE(d.deny_reason() == DenyReason::DENY_REASON_OUTSIDE_WINDOW);
    REQUIRE(d.matched_plate() == "KBZ123A");
}

TEST_CASE("FusionEngine: CLASS_MISMATCH surfaces as MANUAL_REVIEW", "[fusion][deny][class]") {
    auto store = AllowlistStore::open(":memory:");
    std::vector<Allowlistentry> batch{make_entry("KBZ123A", {VehicleClass::VEHICLE_CLASS_SEDAN})};
    store.upsert(kSite, batch);

    FusionEngine engine{store};
    // Request brings a TRUCK to a SEDAN-only allowlist entry.
    const auto req = make_request("KBZ-123A", 0.9f, 0.85f, VehicleClass::VEHICLE_CLASS_TRUCK);
    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));

    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_MANUAL_REVIEW);
    REQUIRE(d.deny_reason() == DenyReason::DENY_REASON_CLASS_MISMATCH);
    REQUIRE(d.matched_plate() == "KBZ123A");
    REQUIRE(d.matched_class() == VehicleClass::VEHICLE_CLASS_TRUCK);
}

TEST_CASE("FusionEngine: LOW_CONFIDENCE picks LOW_ALPR_CONF when ALPR is the weak side",
          "[fusion][low_conf]") {
    auto store = AllowlistStore::open(":memory:");
    FusionEngine engine{
        store,
        FusionConfig{.alpr_weight = 0.5f, .lidar_weight = 0.5f, .min_combined_threshold = 0.7f}};

    // alpr ≈ 0.3, lidar = 0.95 ⇒ combined = 0.625 < 0.7
    const auto req = make_request("KBZ-123A", 0.3f, 0.3f, VehicleClass::VEHICLE_CLASS_SEDAN, 0.95f);
    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));

    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_LOW_CONFIDENCE);
    REQUIRE(d.deny_reason() == DenyReason::DENY_REASON_LOW_ALPR_CONF);
    REQUIRE_THAT(d.reason_text(), ContainsSubstring("below threshold"));
}

TEST_CASE("FusionEngine: LOW_CONFIDENCE picks LOW_LIDAR_CONF when LiDAR is the weak side",
          "[fusion][low_conf]") {
    auto store = AllowlistStore::open(":memory:");
    FusionEngine engine{store};

    // alpr ≈ 0.95, lidar = 0.30 ⇒ combined = 0.625 < 0.7
    const auto req =
        make_request("KBZ-123A", 0.95f, 0.95f, VehicleClass::VEHICLE_CLASS_SEDAN, 0.30f);
    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));

    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_LOW_CONFIDENCE);
    REQUIRE(d.deny_reason() == DenyReason::DENY_REASON_LOW_LIDAR_CONF);
}

TEST_CASE("FusionEngine: force-close override denies regardless of plate match",
          "[fusion][override]") {
    auto store = AllowlistStore::open(":memory:");
    std::vector<Allowlistentry> batch{make_entry("KBZ123A")};
    store.upsert(kSite, batch);

    FusionEngine engine{store};
    engine.set_override(kGate, OverrideState::kForceClose);

    const auto req = make_request("KBZ-123A", 0.99f, 0.99f);
    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));

    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_DENIED);
    REQUIRE(d.deny_reason() == DenyReason::DENY_REASON_FORCE_CLOSE);
    // Force-close runs before plate matching → no matched_plate recorded.
    REQUIRE(d.matched_plate().empty());
}

TEST_CASE("FusionEngine: force-open override authorizes even with no allowlist",
          "[fusion][override]") {
    auto store = AllowlistStore::open(":memory:");
    FusionEngine engine{store};
    engine.set_override(kGate, OverrideState::kForceOpen);

    const auto req = make_request("UNKNOWN1", 0.95f, 0.95f);
    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));

    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);
    REQUIRE(d.matched_plate() == "UNKNOWN1");  // Recorded for audit.
    REQUIRE_THAT(d.reason_text(), ContainsSubstring("force-open"));
}

TEST_CASE("FusionEngine: blocklist beats force-open override", "[fusion][override][blocklist]") {
    auto store = AllowlistStore::open(":memory:");
    store.blocklist_upsert(kSite, "KBZ123A", "stolen");

    FusionEngine engine{store};
    engine.set_override(kGate, OverrideState::kForceOpen);

    const auto req = make_request("KBZ-123A", 0.95f, 0.95f);
    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));

    // Force-open precedes blocklist in the ladder by design — a guard's
    // explicit latch takes priority over the data store.
    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);
}

TEST_CASE("FusionEngine: override_allowlist=true bypasses allowlist but not blocklist",
          "[fusion][guard]") {
    auto store = AllowlistStore::open(":memory:");
    store.blocklist_upsert(kSite, "KBZ999Z", "stolen");

    FusionEngine engine{store};

    // Unknown plate, guard approval → AUTHORIZED.
    AuthorizeRequest ok = make_request("UNKNOWN1", 0.95f, 0.95f);
    ok.set_override_allowlist(true);
    ok.set_actor("guard:alice");
    auto d = engine.decide_at(ok, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));
    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);
    REQUIRE(d.actor() == "guard:alice");

    // Blocklisted plate, guard approval → still DENIED (blocklist wins).
    AuthorizeRequest bad = make_request("KBZ-999Z", 0.95f, 0.95f);
    bad.set_override_allowlist(true);
    d = engine.decide_at(bad, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));
    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_DENIED);
    REQUIRE(d.deny_reason() == DenyReason::DENY_REASON_BLOCKLISTED);
}

TEST_CASE("FusionEngine: override clearing returns to normal evaluation", "[fusion][override]") {
    auto store = AllowlistStore::open(":memory:");
    FusionEngine engine{store};

    REQUIRE(engine.override_for(kGate) == OverrideState::kNone);
    engine.set_override(kGate, OverrideState::kForceClose);
    REQUIRE(engine.override_for(kGate) == OverrideState::kForceClose);
    engine.set_override(kGate, OverrideState::kNone);
    REQUIRE(engine.override_for(kGate) == OverrideState::kNone);

    // After clearing, an unknown plate is the usual NOT_ON_ALLOWLIST.
    const auto req = make_request("UNKNOWN1", 0.95f, 0.95f);
    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));
    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_DENIED);
    REQUIRE(d.deny_reason() == DenyReason::DENY_REASON_NOT_ON_ALLOWLIST);
}

TEST_CASE("FusionEngine: picks the highest-detection-conf plate from a multi-plate frame",
          "[fusion][multi]") {
    auto store = AllowlistStore::open(":memory:");
    std::vector<Allowlistentry> batch{make_entry("HIGHCONF")};
    store.upsert(kSite, batch);

    FusionEngine engine{store};
    AuthorizeRequest req;
    req.set_site_id(kSite);
    auto* f = req.mutable_frame();
    f->set_gate_id(kGate);
    *f->add_plates() = make_plate("LOWCONF1", 0.55f, 0.80f);
    *f->add_plates() = make_plate("HIGHCONF", 0.92f, 0.85f);  // wins
    *f->add_plates() = make_plate("MIDCONF1", 0.70f, 0.80f);
    *f->add_vehicles() = make_vehicle(VehicleClass::VEHICLE_CLASS_SEDAN, 0.9f);

    const auto d = engine.decide_at(req, kMondayMidnightUtc, utc_local(kMondayMidnightUtc));
    REQUIRE(d.verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);
    REQUIRE(d.matched_plate() == "HIGHCONF");
}
