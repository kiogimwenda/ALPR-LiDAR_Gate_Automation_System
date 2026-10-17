// lidar_detector_test.cpp — LiDAR path requirements on ray-cast scenes.
//
// Every scene is rendered by SyntheticSource at the Unitree L1's point
// rate (21,600 pts/s, random directions over its hemisphere, 2 cm range
// noise) from a pillar mount, so density, occlusion and partial views
// are realistic. The requirements are those of docs/lidar.md (from ADR-006):
// vehicles in the gate zone are detected and classified, a car is told
// from an SUV at 10 m, motion is tracked, and update() fits its budget.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <vector>

#include "vision/fused_source.hpp"
#include "vision/lidar/lidar_config.hpp"
#include "vision/lidar/lidar_worker.hpp"
#include "vision/lidar/pcd_source.hpp"
#include "vision/lidar/synthetic_source.hpp"
#include "vision/lidar/vehicle_detector.hpp"

using namespace gate::vision;
using namespace gate::vision::lidar;
using Catch::Matchers::WithinAbs;

namespace {

// L1 on the left gate pillar: 1.2 m up, 2 m left of the centreline,
// its dome axis pointing out along the lane and 20° down.
const Pose kPillar{0.0f, -2.0f, 1.2f, 0.0f, 1.92f, 0.0f};

SceneObject car(float l, float w, float h, float x, float y = 0.0f) {
    SceneObject o;
    o.shape = SceneObject::Shape::kCar;
    o.length = l, o.width = w, o.height = h, o.x = x, o.y = y;
    return o;
}
SceneObject box(float l, float w, float h, float x, float y = 0.0f) {
    SceneObject o = car(l, w, h, x, y);
    o.shape = SceneObject::Shape::kBox;
    return o;
}

const auto kT0 = std::chrono::system_clock::time_point{} + std::chrono::hours{24 * 365 * 56};

SceneConfig scene_of(std::vector<SceneObject> objects, std::uint32_t seed = 7) {
    SceneConfig s;
    s.mount = kPillar;
    s.realtime = false;
    s.seed = seed;
    s.start = kT0;
    s.objects = std::move(objects);
    return s;
}

DetectorConfig detector_cfg() {
    DetectorConfig c;
    c.mount = kPillar;
    return c;
}

// Runs `seconds` of the scene through a detector; returns the final report.
std::vector<VehicleObservation> run(VehicleDetector& det, SyntheticSource& src, float seconds) {
    std::vector<VehicleObservation> out;
    const int batches = static_cast<int>(seconds * 10.0f);
    for (int i = 0; i < batches; ++i) {
        out = det.update(*src.next());
    }
    return out;
}

std::vector<VehicleObservation> run_scene(std::vector<SceneObject> objects, float seconds,
                                          DetectorConfig cfg = detector_cfg()) {
    SyntheticSource src{scene_of(std::move(objects))};
    VehicleDetector det{std::move(cfg)};
    return run(det, src, seconds);
}

}  // namespace

TEST_CASE("an empty driveway reports nothing and finds the ground", "[lidar]") {
    SyntheticSource src{scene_of({})};
    VehicleDetector det{detector_cfg()};
    const auto out = run(det, src, 2.0f);
    CHECK(out.empty());
    CHECK(det.stats().window_points > 1000);
    CHECK_THAT(det.stats().ground_c, WithinAbs(0.0, 0.05));
    CHECK_THAT(det.stats().ground_a, WithinAbs(0.0, 0.02));
}

TEST_CASE("a sedan at the gate is detected, measured and classified", "[lidar]") {
    const auto out = run_scene({car(4.5f, 1.75f, 1.45f, 5.0f)}, 2.0f);
    REQUIRE(out.size() == 1);
    const auto& v = out.front();
    INFO("L " << v.box.length << " W " << v.box.width << " H " << v.box.height << " at "
              << v.box.center_x << "," << v.box.center_y << " conf " << v.confidence);
    CHECK(v.vehicle_class == VehicleClass::kSedan);
    CHECK(v.confidence >= 0.6f);
    CHECK_THAT(v.box.height, WithinAbs(1.45, 0.12));
    // Length is a lower bound: from the pillar the low boot is barely seen.
    CHECK(v.box.length >= 3.6f);
    CHECK(v.box.length <= 4.8f);
    CHECK_THAT(v.box.width, WithinAbs(1.75, 0.35));
    CHECK_THAT(v.box.center_x, WithinAbs(5.0, 0.5));
    CHECK_THAT(v.box.center_y, WithinAbs(0.0, 0.4));
    CHECK(v.track_id != 0);
    CHECK(v.point_count >= 40);
}

TEST_CASE("car vs SUV is distinguished at 10 m (ADR-006)", "[lidar]") {
    for (std::uint32_t seed : {1u, 2u, 3u}) {
        INFO("seed " << seed);
        SceneConfig s = scene_of({car(4.5f, 1.75f, 1.45f, 10.0f)}, seed);
        SyntheticSource sedan_src{s};
        VehicleDetector a{detector_cfg()};
        const auto sedan = run(a, sedan_src, 3.0f);
        REQUIRE(sedan.size() == 1);
        INFO("sedan H " << sedan[0].box.height);
        CHECK(sedan[0].vehicle_class == VehicleClass::kSedan);

        s.objects = {car(4.7f, 1.85f, 1.78f, 10.0f)};
        SyntheticSource suv_src{s};
        VehicleDetector b{detector_cfg()};
        const auto suv = run(b, suv_src, 3.0f);
        REQUIRE(suv.size() == 1);
        INFO("suv H " << suv[0].box.height);
        CHECK(suv[0].vehicle_class == VehicleClass::kSuv);
    }
}

TEST_CASE("vans and trucks fall in their own classes", "[lidar]") {
    const auto van = run_scene({box(5.3f, 1.88f, 2.28f, 6.0f)}, 2.0f);  // Hiace-like
    REQUIRE(van.size() == 1);
    CHECK(van[0].vehicle_class == VehicleClass::kVan);

    DetectorConfig wide = detector_cfg();
    wide.trigger_zone.x_max = 15.0f;
    const auto truck = run_scene({box(7.0f, 2.2f, 3.0f, 8.0f)}, 2.0f, wide);  // NQR-like
    REQUIRE(truck.size() == 1);
    CHECK(truck[0].vehicle_class == VehicleClass::kTruck);
}

TEST_CASE("an approaching car keeps one track and a sensible velocity", "[lidar]") {
    SceneObject c = car(4.5f, 1.75f, 1.45f, 16.0f);
    c.vx = -2.0f;           // 7 km/h towards the gate
    c.stop_after_s = 5.0f;  // stops at x = 6
    SyntheticSource src{scene_of({c})};
    VehicleDetector det{detector_cfg()};

    std::uint32_t id = 0;
    bool saw_moving = false;
    for (int i = 0; i < 70; ++i) {
        const auto out = det.update(*src.next());
        if (out.empty()) {
            continue;  // still outside the 12 m trigger zone
        }
        REQUIRE(out.size() == 1);
        if (id == 0) {
            id = out[0].track_id;
        }
        CHECK(out[0].track_id == id);
        const float t = static_cast<float>(i) / 10.0f;
        if (t > 3.5f && t < 4.8f) {  // well inside the zone, still moving
            saw_moving = true;
            CHECK_THAT(out[0].velocity_x, WithinAbs(-2.0, 0.7));
        }
    }
    CHECK(saw_moving);
    // Stopped: velocity settles, the class is a sedan despite the smear.
    const auto& last = det.current();
    REQUIRE(last.size() == 1);
    CHECK(std::abs(last[0].velocity_x) < 0.5f);
    CHECK(last[0].vehicle_class == VehicleClass::kSedan);
    CHECK_THAT(last[0].box.center_x, WithinAbs(6.0, 0.6));
}

TEST_CASE("only the trigger zone is reported", "[lidar]") {
    CHECK(run_scene({car(4.5f, 1.75f, 1.45f, 15.0f)}, 2.0f).empty());       // beyond x_max 12
    CHECK(run_scene({car(4.5f, 1.75f, 1.45f, 6.0f, 4.0f)}, 2.0f).empty());  // next lane
}

TEST_CASE("a pedestrian beside a car is a separate object", "[lidar]") {
    const auto out =
        run_scene({car(4.5f, 1.75f, 1.45f, 6.0f), box(0.5f, 0.5f, 1.75f, 6.0f, 1.8f)}, 2.0f);
    REQUIRE(out.size() == 2);
    int sedans = 0, people = 0;
    for (const auto& v : out) {
        sedans += v.vehicle_class == VehicleClass::kSedan;
        people += v.vehicle_class == VehicleClass::kPedestrian;
    }
    CHECK(sedans == 1);
    CHECK(people == 1);
}

TEST_CASE("exclude zones mask fixed objects", "[lidar]") {
    const SceneObject planter = box(1.0f, 1.0f, 0.9f, 3.0f, 2.0f);
    CHECK_FALSE(run_scene({planter}, 1.5f).empty());
    DetectorConfig masked = detector_cfg();
    masked.exclude_zones.push_back({2.3f, 3.7f, 1.3f, 2.7f, -1.0f, 3.0f});
    CHECK(run_scene({planter}, 1.5f, masked).empty());
}

TEST_CASE("a mounting error of a few degrees is absorbed by the ground fit", "[lidar]") {
    SceneConfig s = scene_of({car(4.5f, 1.75f, 1.45f, 6.0f)});
    s.mount.roll = 0.05f;  // the bracket is 3° off from what was commissioned
    SyntheticSource src{s};
    VehicleDetector det{detector_cfg()};  // believes roll = 0
    const auto out = run(det, src, 2.0f);
    REQUIRE(out.size() == 1);
    CHECK(out[0].vehicle_class == VehicleClass::kSedan);
}

TEST_CASE("update() meets the 20 ms latency budget", "[lidar][perf]") {
    SceneObject moving = car(4.5f, 1.75f, 1.45f, 14.0f);
    moving.vx = -2.0f;
    moving.stop_after_s = 4.0f;
    SceneConfig s = scene_of(
        {moving, box(0.5f, 0.5f, 1.75f, 5.0f, 1.8f), car(4.7f, 1.85f, 1.78f, 9.0f, -3.0f)});
    s.points_per_second = 150'000.0f;  // ~7x the L1, so 25k-point windows after cropping
    SyntheticSource src{s};
    VehicleDetector det{detector_cfg()};
    std::vector<double> ms;
    std::size_t peak = 0;
    for (int i = 0; i < 100; ++i) {
        det.update(*src.next());
        ms.push_back(det.stats().update_ms);
        peak = std::max(peak, det.stats().window_points);
    }
    std::sort(ms.begin(), ms.end());
    INFO("p99 " << ms[98] << " ms over windows of up to " << peak << " points");
    CHECK(peak >= 25'000);
    CHECK(ms[98] <= 20.0);
}

TEST_CASE("dimension envelopes classify reference vehicles", "[lidar][classify]") {
    CHECK(classify_dimensions(4.4f, 1.70f, 1.46f).best == VehicleClass::kSedan);   // Axio
    CHECK(classify_dimensions(4.6f, 1.85f, 1.69f).best == VehicleClass::kSuv);     // RAV4
    CHECK(classify_dimensions(5.3f, 1.85f, 1.80f).best == VehicleClass::kPickup);  // Hilux
    CHECK(classify_dimensions(5.38f, 1.88f, 2.28f).best == VehicleClass::kVan);    // Hiace
    CHECK(classify_dimensions(7.0f, 2.2f, 3.0f).best == VehicleClass::kTruck);     // NQR
    CHECK(classify_dimensions(0.5f, 0.5f, 1.75f).best == VehicleClass::kPedestrian);
    CHECK(classify_dimensions(2.1f, 0.8f, 1.3f).best == VehicleClass::kMotorcycle);
    const auto junk = classify_dimensions(0.3f, 0.3f, 0.45f);  // a bin
    CHECK(junk.raw < 0.05f);
}

TEST_CASE("PCD files round-trip (binary) and parse (ascii)", "[lidar][pcd]") {
    namespace fs = std::filesystem;
    const fs::path dir =
        fs::temp_directory_path() / ("gate-pcd-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const std::vector<Point> pts{{1.0f, 2.0f, 3.0f, 10.0f}, {-1.5f, 0.25f, 0.0f, 0.0f}};
    write_pcd(dir / "a.pcd", pts);
    const auto back = read_pcd(dir / "a.pcd");
    REQUIRE(back.size() == 2);
    CHECK(back[1].x == -1.5f);
    CHECK(back[0].intensity == 10.0f);

    {
        std::ofstream f{dir / "b.pcd"};
        f << "VERSION 0.7\nFIELDS x y z rgb\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\n"
             "WIDTH 2\nHEIGHT 1\nPOINTS 2\nDATA ascii\n0.5 0.5 1.0 0\n2 3 4 0\n";
    }
    const auto ascii = read_pcd(dir / "b.pcd");
    REQUIRE(ascii.size() == 2);
    CHECK(ascii[1].z == 4.0f);

    {
        std::ofstream f{dir / "c.pcd"};
        f << "FIELDS x y\nSIZE 4 4\nTYPE F F\nPOINTS 0\nDATA ascii\n";
    }
    CHECK_THROWS(read_pcd(dir / "c.pcd"));  // no z

    PcdReplaySource replay{{dir, 10.0f, false, 1}};
    int n = 0;
    while (replay.next()) {
        ++n;
    }
    CHECK(n == 2);                          // c.pcd fails mid-replay ...
    CHECK_FALSE(replay.failure().empty());  // ... and that is a fault, not a clean end
    fs::remove_all(dir);
}

TEST_CASE("site config is fail-closed", "[lidar][config]") {
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() /
                       ("gate-lidar-" + std::to_string(std::random_device{}()) + ".json");
    {
        std::ofstream f{p};
        f << R"({"mount": {"z": 1.2, "pitch": 1.92}, "trigger_zone": {"x_min": 2, "x_max": 9},
                "exclude_zones": [{"x_min": 0, "x_max": 1, "y_min": 2, "y_max": 3, "z_min": 0, "z_max": 2}],
                "window_ms": 300})";
    }
    const auto c = load_detector_config(p);
    CHECK(c.mount.z == 1.2f);
    CHECK(c.trigger_zone.x_max == 9.0f);
    CHECK(c.trigger_zone.y_min == -2.5f);  // unspecified edges keep their defaults
    CHECK(c.exclude_zones.size() == 1);
    CHECK(c.window.count() == 300);

    for (const char* bad :
         {R"({"trigger_zon": {}})", R"({"mount": {"pitch": "1.9"}})",
          R"({"trigger_zone": {"x_min": 9, "x_max": 2}})", R"({"window_ms": -5})", R"([1,2])"}) {
        INFO(bad);
        {
            std::ofstream f{p, std::ios::trunc};
            f << bad;
        }
        CHECK_THROWS(load_detector_config(p));
    }
    std::filesystem::remove(p);
}

namespace {

// Primary source stub: one plate frame per call at a controllable time.
class PlateTicks final : public ObservationSource {
public:
    explicit PlateTicks(std::vector<std::chrono::system_clock::time_point> ts)
        : ts_(std::move(ts)) {}
    std::optional<ObservationSet> next() override {
        if (i_ == ts_.size()) {
            return std::nullopt;
        }
        ObservationSet s;
        s.capture_ts = ts_[i_++];
        s.plates.push_back({"KDA123A", 0.95f, 0.93f, {}, "KE", "KDA 123A"});
        s.vehicles.push_back({});  // a scenario's own vehicle: must be replaced
        return s;
    }

private:
    std::vector<std::chrono::system_clock::time_point> ts_;
    std::size_t i_ = 0;
};

}  // namespace

TEST_CASE("FusedSource attaches LiDAR vehicles by timestamp", "[lidar][fused]") {
    SceneConfig s = scene_of({car(4.5f, 1.75f, 1.45f, 6.0f)});
    auto worker =
        std::make_shared<LidarWorker>(std::make_unique<SyntheticSource>(s),
                                      VehicleDetector{detector_cfg()}, LidarWorker::Config{});
    for (int i = 0; i < 20; ++i) {
        REQUIRE(worker->step());
    }
    const auto lidar_ts = worker->latest().stamp;  // kT0 + 1.9 s

    FusedSource fused{std::make_unique<PlateTicks>(
                          std::vector{lidar_ts + std::chrono::milliseconds{100},    // paired
                                      lidar_ts + std::chrono::milliseconds{900}}),  // too far apart
                      worker, FusedSource::Config{std::chrono::milliseconds{300}}};
    const auto a = fused.next();
    REQUIRE(a);
    REQUIRE(a->vehicles.size() == 1);
    CHECK(a->vehicles[0].vehicle_class == VehicleClass::kSedan);
    const auto b = fused.next();
    REQUIRE(b);
    CHECK(b->vehicles.empty());  // the scenario's vehicle was dropped, LiDAR too stale
    CHECK(fused.counters().skew_misses == 1);
    CHECK_FALSE(fused.next());
    CHECK(fused.failure().empty());  // primary ended cleanly
}

TEST_CASE("a silent or ended LiDAR is a fault that ends the stream", "[lidar][fused]") {
    SceneConfig s = scene_of({});
    s.duration_s = 0.2f;  // two batches, then the "device" is gone
    auto worker =
        std::make_shared<LidarWorker>(std::make_unique<SyntheticSource>(s),
                                      VehicleDetector{detector_cfg()}, LidarWorker::Config{});
    CHECK(worker->step());
    CHECK(worker->step());
    CHECK(worker->fault().empty());
    CHECK_FALSE(worker->step());
    CHECK_FALSE(worker->fault().empty());

    FusedSource fused{std::make_unique<PlateTicks>(std::vector{kT0}), worker, {}};
    CHECK_FALSE(fused.next());
    CHECK(fused.failure().find("LiDAR stream ended") != std::string::npos);

    // Silence: a worker whose source never delivers goes stale.
    LidarWorker::Config quick;
    quick.stale_after = std::chrono::milliseconds{50};
    LidarWorker idle{std::make_unique<SyntheticSource>(scene_of({})),
                     VehicleDetector{detector_cfg()}, quick};
    std::this_thread::sleep_for(std::chrono::milliseconds{80});
    CHECK(idle.fault().find("no LiDAR data") != std::string::npos);
}
