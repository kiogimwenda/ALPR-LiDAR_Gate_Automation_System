// scenario_source_test.cpp — fail-closed parsing + fake-clock pacing.
//
// Every case drives ScenarioSource through a FakeClock, so no test
// ever really sleeps: sleep_until() simply advances the fake time and
// records the target, letting the pacing assertions read back exactly
// when each event would have fired.

#include "vision/scenario_source.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using gate::vision::Clock;
using gate::vision::ObservationSet;
using gate::vision::ScenarioSource;
using gate::vision::VehicleClass;

using Catch::Matchers::ContainsSubstring;

namespace {

// Instant clock: sleep_until() jumps time forward and records the
// requested wake-ups. Wall time tracks steady time from a fixed epoch
// so capture_ts values are deterministic too.
class FakeClock final : public Clock {
public:
    std::chrono::steady_clock::time_point now() override { return now_; }

    std::chrono::system_clock::time_point wall_now() override {
        return wall_epoch_ + (now_ - steady_epoch_);
    }

    void sleep_until(std::chrono::steady_clock::time_point tp) override {
        wakeups.push_back(tp);
        if (tp > now_) {
            now_ = tp;
        }
    }

    std::vector<std::chrono::steady_clock::time_point> wakeups;

private:
    std::chrono::steady_clock::time_point steady_epoch_ = std::chrono::steady_clock::time_point{};
    std::chrono::steady_clock::time_point now_ = steady_epoch_;
    std::chrono::system_clock::time_point wall_epoch_ =
        std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}};
};

const std::string kHappyScenario = R"({
  "loop": 2,
  "events": [
    {
      "offset_ms": 0,
      "plates": [
        {
          "text": "KDA123X",
          "detection_conf": 0.94,
          "ocr_conf": 0.91,
          "box": {"x": 100, "y": 220, "width": 180, "height": 60},
          "country_hint": "KE",
          "raw_ocr": "KDA 123X"
        }
      ],
      "vehicles": [
        {
          "class": "sedan",
          "confidence": 0.88,
          "box": {"center_x": 4.5, "center_y": -0.3, "center_z": 0.8,
                  "length": 4.6, "width": 1.8, "height": 1.5,
                  "yaw_radians": 0.05},
          "track_id": 7,
          "point_count": 412
        }
      ]
    },
    {"offset_ms": 250, "vehicles": [{"class": "truck", "confidence": 0.75}]},
    {"offset_ms": 600, "plates": [{"text": "KBZ999Q", "detection_conf": 0.5, "ocr_conf": 0.4}]}
  ]
})";

}  // namespace

TEST_CASE("scenario happy path parses every field", "[vision][scenario]") {
    auto clock = std::make_shared<FakeClock>();
    auto src = ScenarioSource::from_json(kHappyScenario, clock);
    CHECK(src.event_count() == 3);
    CHECK(src.loop_count() == 2);

    const auto first = src.next();
    REQUIRE(first.has_value());
    REQUIRE(first->plates.size() == 1);
    const auto& p = first->plates[0];
    CHECK(p.text == "KDA123X");
    CHECK(p.detection_conf == 0.94f);
    CHECK(p.ocr_conf == 0.91f);
    CHECK(p.box.x == 100);
    CHECK(p.box.y == 220);
    CHECK(p.box.width == 180);
    CHECK(p.box.height == 60);
    CHECK(p.country_hint == "KE");
    CHECK(p.raw_ocr == "KDA 123X");

    REQUIRE(first->vehicles.size() == 1);
    const auto& v = first->vehicles[0];
    CHECK(v.vehicle_class == VehicleClass::kSedan);
    CHECK(v.confidence == 0.88f);
    CHECK(v.box.center_x == 4.5f);
    CHECK(v.box.length == 4.6f);
    CHECK(v.box.yaw_radians == 0.05f);
    CHECK(v.track_id == 7);
    CHECK(v.point_count == 412);

    // Optional sections default cleanly rather than erroring.
    const auto second = src.next();
    REQUIRE(second.has_value());
    CHECK(second->plates.empty());
    REQUIRE(second->vehicles.size() == 1);
    CHECK(second->vehicles[0].vehicle_class == VehicleClass::kTruck);
}

TEST_CASE("scenario from_file round-trips and rejects missing files", "[vision][scenario]") {
    const auto path = std::filesystem::temp_directory_path() / "gate_vision_scenario_test.json";
    {
        std::ofstream out(path);
        out << kHappyScenario;
    }
    auto src = ScenarioSource::from_file(path, std::make_shared<FakeClock>());
    CHECK(src.event_count() == 3);
    std::filesystem::remove(path);

    CHECK_THROWS_WITH(ScenarioSource::from_file(path), ContainsSubstring("cannot read file"));
}

TEST_CASE("scenario rejects malformed JSON", "[vision][scenario][fail-closed]") {
    CHECK_THROWS_WITH(ScenarioSource::from_json("{ not json"), ContainsSubstring("malformed JSON"));
    CHECK_THROWS_WITH(ScenarioSource::from_json("[]"),
                      ContainsSubstring("top level must be an object"));
}

TEST_CASE("scenario rejects missing required fields", "[vision][scenario][fail-closed]") {
    // No events at all.
    CHECK_THROWS_WITH(ScenarioSource::from_json(R"({"loop": 1})"),
                      ContainsSubstring("missing required field 'events'"));
    // Empty events array is refused too — almost certainly a bad file.
    CHECK_THROWS_WITH(ScenarioSource::from_json(R"({"events": []})"),
                      ContainsSubstring("non-empty"));
    // Event without an offset.
    CHECK_THROWS_WITH(ScenarioSource::from_json(R"({"events": [{"plates": []}]})"),
                      ContainsSubstring("missing required field 'offset_ms'"));
    // Plate without OCR confidence — message names the exact path.
    CHECK_THROWS_WITH(ScenarioSource::from_json(
                          R"({"events": [{"offset_ms": 0,
                "plates": [{"text": "KDA123X", "detection_conf": 0.9}]}]})"),
                      ContainsSubstring("events[0].plates[0]") && ContainsSubstring("ocr_conf"));
    // Vehicle without a class.
    CHECK_THROWS_WITH(ScenarioSource::from_json(R"({"events": [{"offset_ms": 0,
                                     "vehicles": [{"confidence": 0.9}]}]})"),
                      ContainsSubstring("missing required field 'class'"));
    // Unknown class string never maps silently to kUnknown.
    CHECK_THROWS_WITH(ScenarioSource::from_json(R"({"events": [{"offset_ms": 0,
                                     "vehicles": [{"class": "hovercraft", "confidence": 0.9}]}]})"),
                      ContainsSubstring("unknown vehicle class 'hovercraft'"));
}

TEST_CASE("scenario rejects negative offsets", "[vision][scenario][fail-closed]") {
    CHECK_THROWS_WITH(ScenarioSource::from_json(R"({"events": [{"offset_ms": -1}]})"),
                      ContainsSubstring("must be >= 0"));
}

TEST_CASE("scenario rejects out-of-range confidences", "[vision][scenario][fail-closed]") {
    CHECK_THROWS_WITH(ScenarioSource::from_json(
                          R"({"events": [{"offset_ms": 0,
                "plates": [{"text": "A", "detection_conf": 1.2, "ocr_conf": 0.5}]}]})"),
                      ContainsSubstring("confidence must be in [0, 1]"));
    CHECK_THROWS_WITH(ScenarioSource::from_json(
                          R"({"events": [{"offset_ms": 0,
                "vehicles": [{"class": "van", "confidence": -0.1}]}]})"),
                      ContainsSubstring("confidence must be in [0, 1]"));
}

TEST_CASE("scenario rejects a bad loop count", "[vision][scenario][fail-closed]") {
    CHECK_THROWS_WITH(ScenarioSource::from_json(R"({"loop": 0, "events": [{"offset_ms": 0}]})"),
                      ContainsSubstring("must be an integer >= 1"));
}

TEST_CASE("scenario paces events by offset and loops re-base", "[vision][scenario][pacing]") {
    using std::chrono::milliseconds;
    auto clock = std::make_shared<FakeClock>();
    auto src = ScenarioSource::from_json(
        R"({"loop": 2, "events": [
             {"offset_ms": 0,   "plates": [{"text": "A1", "detection_conf": 0.9, "ocr_conf": 0.9}]},
             {"offset_ms": 100, "plates": [{"text": "B2", "detection_conf": 0.9, "ocr_conf": 0.9}]},
             {"offset_ms": 400, "plates": [{"text": "C3", "detection_conf": 0.9, "ocr_conf": 0.9}]}
           ]})",
        clock);

    std::vector<std::string> order;
    while (const auto set = src.next()) {
        REQUIRE(set->plates.size() == 1);
        order.push_back(set->plates[0].text);
    }
    // 2 loops × 3 events, in script order, then a clean end-of-stream.
    CHECK(order == std::vector<std::string>{"A1", "B2", "C3", "A1", "B2", "C3"});
    CHECK_FALSE(src.next().has_value());

    // Six wake-ups: iteration one at t=0/100/400, iteration two re-based
    // at the moment it started (t=400), so 400/500/800.
    REQUIRE(clock->wakeups.size() == 6);
    const auto epoch = std::chrono::steady_clock::time_point{};
    CHECK(clock->wakeups[0] == epoch + milliseconds{0});
    CHECK(clock->wakeups[1] == epoch + milliseconds{100});
    CHECK(clock->wakeups[2] == epoch + milliseconds{400});
    CHECK(clock->wakeups[3] == epoch + milliseconds{400});
    CHECK(clock->wakeups[4] == epoch + milliseconds{500});
    CHECK(clock->wakeups[5] == epoch + milliseconds{800});
}

TEST_CASE("scenario stamps capture_ts from the injected clock", "[vision][scenario][pacing]") {
    auto clock = std::make_shared<FakeClock>();
    auto src = ScenarioSource::from_json(
        R"({"events": [
             {"offset_ms": 0,   "plates": [{"text": "A1", "detection_conf": 0.9, "ocr_conf": 0.9}]},
             {"offset_ms": 750, "plates": [{"text": "B2", "detection_conf": 0.9, "ocr_conf": 0.9}]}
           ]})",
        clock);

    const auto first = src.next();
    const auto second = src.next();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    const auto delta = second->capture_ts - first->capture_ts;
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(delta).count() == 750);
}
