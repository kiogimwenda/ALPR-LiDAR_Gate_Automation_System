// allowlist_store_test.cpp — unit tests for the SQLite-backed allowlist.
//
// Every test runs against an in-memory database (":memory:") so no
// filesystem state leaks between cases. The store is move-only, so each
// test owns its own instance.

#include "auth/allowlist_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <ctime>
#include <string>
#include <vector>

using gate::auth::AllowlistStore;
using gate::auth::is_allowed_now;
using gate::auth::normalize_plate;
using gate::v1::Allowlistentry;
using gate::v1::TimeWindow;
using gate::v1::VehicleClass;

namespace {

Allowlistentry make_entry(std::string plate, std::string owner = "Alice") {
    Allowlistentry e;
    e.set_plate_text(std::move(plate));
    e.set_owner_name(std::move(owner));
    return e;
}

std::tm utc_local(std::time_t t) {
    std::tm out{};
    gmtime_r(&t, &out);
    return out;
}

}  // namespace

TEST_CASE("normalize_plate: uppercases ASCII and strips whitespace and dashes",
          "[allowlist][normalize]") {
    REQUIRE(normalize_plate("kbz 123a") == "KBZ123A");
    REQUIRE(normalize_plate("  kbz-123a ") == "KBZ123A");
    REQUIRE(normalize_plate("kbz_123a") == "KBZ123A");
    REQUIRE(normalize_plate("KBZ123A") == "KBZ123A");
    // Non-ASCII bytes pass through verbatim — internationalized plates round-trip.
    REQUIRE(normalize_plate("kΩ7") == "KΩ7");
}

TEST_CASE("AllowlistStore: upsert + lookup round-trips entry verbatim",
          "[allowlist][crud]") {
    auto store = AllowlistStore::open(":memory:");

    auto e = make_entry("kbz 123a", "Alice Resident");
    e.set_owner_unit("Apt 4B");
    e.set_notes("primary vehicle");
    e.add_allowed_classes(VehicleClass::VEHICLE_CLASS_SEDAN);
    e.add_allowed_classes(VehicleClass::VEHICLE_CLASS_SUV);
    auto* w = e.add_time_windows();
    w->set_start_minute_of_day(6 * 60);   // 06:00
    w->set_end_minute_of_day(22 * 60);    // 22:00
    w->set_days_of_week_mask(0b0011111);  // Mon–Fri

    std::vector<Allowlistentry> batch{e};
    const auto stats = store.upsert("site-a", batch);
    REQUIRE(stats.inserted == 1);
    REQUIRE(stats.updated == 0);

    const auto got = store.lookup("site-a", "KBZ-123A");
    REQUIRE(got.has_value());
    REQUIRE(got->plate_text() == "KBZ123A");
    REQUIRE(got->owner_name() == "Alice Resident");
    REQUIRE(got->owner_unit() == "Apt 4B");
    REQUIRE(got->allowed_classes_size() == 2);
    REQUIRE(got->time_windows_size() == 1);
    REQUIRE(got->time_windows(0).start_minute_of_day() == 360);
    REQUIRE(got->time_windows(0).days_of_week_mask() == 0b0011111u);
}

TEST_CASE("AllowlistStore: upsert with same key updates rather than inserts",
          "[allowlist][crud]") {
    auto store = AllowlistStore::open(":memory:");

    auto e = make_entry("KBZ123A", "Alice");
    std::vector<Allowlistentry> batch{e};
    const auto first = store.upsert("site-a", batch);
    REQUIRE(first.inserted == 1);
    REQUIRE(first.updated == 0);

    e.set_owner_name("Alice Renamed");
    e.add_allowed_classes(VehicleClass::VEHICLE_CLASS_PICKUP);
    batch[0] = e;
    const auto second = store.upsert("site-a", batch);
    REQUIRE(second.inserted == 0);
    REQUIRE(second.updated == 1);

    const auto got = store.lookup("site-a", "KBZ123A");
    REQUIRE(got.has_value());
    REQUIRE(got->owner_name() == "Alice Renamed");
    REQUIRE(got->allowed_classes_size() == 1);
    REQUIRE(got->allowed_classes(0) == VehicleClass::VEHICLE_CLASS_PICKUP);
}

TEST_CASE("AllowlistStore: lookup returns nullopt for unknown plate or wrong site",
          "[allowlist][crud]") {
    auto store = AllowlistStore::open(":memory:");

    std::vector<Allowlistentry> batch{make_entry("KBZ123A")};
    store.upsert("site-a", batch);

    REQUIRE_FALSE(store.lookup("site-a", "ZZZ999Z").has_value());
    REQUIRE_FALSE(store.lookup("other-site", "KBZ123A").has_value());
}

TEST_CASE("AllowlistStore: remove deletes rows and cascades sub-rows",
          "[allowlist][crud]") {
    auto store = AllowlistStore::open(":memory:");

    auto e = make_entry("KBZ123A");
    e.add_allowed_classes(VehicleClass::VEHICLE_CLASS_SEDAN);
    auto* w = e.add_time_windows();
    w->set_start_minute_of_day(0);
    w->set_end_minute_of_day(1440);
    w->set_days_of_week_mask(0x7F);
    std::vector<Allowlistentry> batch{e};
    store.upsert("site-a", batch);

    std::vector<std::string> to_remove{"KBZ-123A", "NOT-PRESENT"};
    const auto removed = store.remove("site-a", to_remove);
    REQUIRE(removed == 1);
    REQUIRE_FALSE(store.lookup("site-a", "KBZ123A").has_value());

    // Cascade — re-inserting the same plate should not see ghost classes/windows.
    store.upsert("site-a", batch);
    const auto re = store.lookup("site-a", "KBZ123A");
    REQUIRE(re.has_value());
    REQUIRE(re->allowed_classes_size() == 1);
    REQUIRE(re->time_windows_size() == 1);
}

TEST_CASE("AllowlistStore: list paginates and orders by plate_text",
          "[allowlist][list]") {
    auto store = AllowlistStore::open(":memory:");

    std::vector<Allowlistentry> batch;
    for (const char* p : {"AAA111", "BBB222", "CCC333", "DDD444", "EEE555"}) {
        batch.push_back(make_entry(p));
    }
    store.upsert("site-a", batch);

    const auto p1 = store.list("site-a", 2, "");
    REQUIRE(p1.entries.size() == 2);
    REQUIRE(p1.entries[0].plate_text() == "AAA111");
    REQUIRE(p1.entries[1].plate_text() == "BBB222");
    REQUIRE_FALSE(p1.next_token.empty());

    const auto p2 = store.list("site-a", 2, p1.next_token);
    REQUIRE(p2.entries.size() == 2);
    REQUIRE(p2.entries[0].plate_text() == "CCC333");
    REQUIRE(p2.entries[1].plate_text() == "DDD444");

    const auto p3 = store.list("site-a", 2, p2.next_token);
    REQUIRE(p3.entries.size() == 1);
    REQUIRE(p3.entries[0].plate_text() == "EEE555");
    REQUIRE(p3.next_token.empty());
}

TEST_CASE("AllowlistStore: blocklist upsert + lookup are site-scoped",
          "[allowlist][blocklist]") {
    auto store = AllowlistStore::open(":memory:");

    store.blocklist_upsert("site-a", "kbz 123a", "stolen vehicle");
    REQUIRE(store.is_blocklisted("site-a", "KBZ-123A"));
    REQUIRE_FALSE(store.is_blocklisted("site-b", "KBZ123A"));

    std::vector<std::string> to_remove{"KBZ123A"};
    REQUIRE(store.blocklist_remove("site-a", to_remove) == 1);
    REQUIRE_FALSE(store.is_blocklisted("site-a", "KBZ123A"));
}

TEST_CASE("is_allowed_now: empty restrictions allow any class at any time",
          "[allowlist][is_allowed]") {
    Allowlistentry e = make_entry("KBZ123A");
    const std::time_t now = 1714000000;  // 2024-04-25 ~ Thursday

    REQUIRE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, now, utc_local(now)));
    REQUIRE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_TRUCK, now, utc_local(now)));
}

TEST_CASE("is_allowed_now: vehicle class must match when allowed_classes is set",
          "[allowlist][is_allowed]") {
    Allowlistentry e = make_entry("KBZ123A");
    e.add_allowed_classes(VehicleClass::VEHICLE_CLASS_SEDAN);

    const std::time_t now = 1714000000;
    REQUIRE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, now, utc_local(now)));
    REQUIRE_FALSE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_TRUCK, now, utc_local(now)));
}

TEST_CASE("is_allowed_now: validity window denies before valid_from and at valid_until",
          "[allowlist][is_allowed]") {
    Allowlistentry e = make_entry("KBZ123A");
    e.mutable_valid_from()->set_seconds(1000);
    e.mutable_valid_until()->set_seconds(2000);

    REQUIRE_FALSE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, 999, utc_local(999)));
    REQUIRE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, 1000, utc_local(1000)));
    REQUIRE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, 1500, utc_local(1500)));
    // Half-open: valid_until is the first denied second.
    REQUIRE_FALSE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, 2000, utc_local(2000)));
}

TEST_CASE("is_allowed_now: time-window matches by minute-of-day and weekday",
          "[allowlist][is_allowed]") {
    Allowlistentry e = make_entry("KBZ123A");
    auto* w = e.add_time_windows();
    w->set_start_minute_of_day(8 * 60);   // 08:00
    w->set_end_minute_of_day(17 * 60);    // 17:00
    w->set_days_of_week_mask(0b0011111);  // Mon..Fri (bit0=Mon)

    // 2024-01-01 is a Monday at 00:00:00 UTC.
    const std::time_t mon_midnight = 1704067200;
    const std::time_t mon_10am = mon_midnight + 10 * 3600;
    const std::time_t mon_5pm = mon_midnight + 17 * 3600;
    const std::time_t sat_10am = mon_midnight + (5 * 86400) + 10 * 3600;

    REQUIRE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, mon_10am,
                           utc_local(mon_10am)));
    // Half-open window — 17:00 is excluded.
    REQUIRE_FALSE(
        is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, mon_5pm, utc_local(mon_5pm)));
    // Saturday is not in the day mask.
    REQUIRE_FALSE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, sat_10am,
                                 utc_local(sat_10am)));
}

TEST_CASE("is_allowed_now: window wraps past midnight when end < start",
          "[allowlist][is_allowed]") {
    Allowlistentry e = make_entry("KBZ123A");
    auto* w = e.add_time_windows();
    w->set_start_minute_of_day(22 * 60);  // 22:00
    w->set_end_minute_of_day(2 * 60);     // 02:00 next day
    w->set_days_of_week_mask(0x7F);       // every day

    const std::time_t mon_midnight = 1704067200;
    const std::time_t mon_2300 = mon_midnight + 23 * 3600;
    const std::time_t mon_0100 = mon_midnight + 1 * 3600;
    const std::time_t mon_1200 = mon_midnight + 12 * 3600;

    REQUIRE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, mon_2300,
                           utc_local(mon_2300)));
    REQUIRE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, mon_0100,
                           utc_local(mon_0100)));
    REQUIRE_FALSE(is_allowed_now(e, VehicleClass::VEHICLE_CLASS_SEDAN, mon_1200,
                                 utc_local(mon_1200)));
}
