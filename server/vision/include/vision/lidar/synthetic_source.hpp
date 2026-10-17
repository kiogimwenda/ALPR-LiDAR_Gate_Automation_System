// synthetic_source.hpp — ray-cast LiDAR simulator (Phase 6.1).
//
// Casts randomly-directed rays (uniform over the sensor's upper
// hemisphere — the Unitree L1's 360° × 90° field of view, whose
// non-repetitive pattern fills in over time) against a ground plane and
// a set of moving vehicle shapes, with range noise, and emits the hits
// in the sensor frame exactly like a real driver would. Occlusion,
// self-shadowing and density fall-off with distance come out of the
// geometry for free.
//
// Used by:
//   * the detector tests (classification at range, tracking, timing);
//   * lidar-bench (the latency requirement);
//   * gate-vision --lidar synthetic:<scene.json> — a camera-less demo of
//     the whole LiDAR path.
//
// Scene JSON (fail-closed; any violation throws std::runtime_error):
//   {
//     "mount": {"x":0, "y":-1.5, "z":1.2, "roll":0, "pitch":1.92, "yaw":0},
//     "points_per_second": 21600,   // optional, L1 default
//     "rate_hz": 10,                // optional batch rate
//     "noise_m": 0.02,              // optional 1-sigma range noise
//     "seed": 1,                    // optional
//     "duration_s": 0,              // optional, 0 = forever
//     "realtime": true,             // optional: pace batches to the wall clock
//     "objects": [
//       {"shape": "car",            // car (body + cabin) | box
//        "length": 4.5, "width": 1.75, "height": 1.45,
//        "x": 15, "y": 0, "yaw": 0,         // gate frame at t = 0, metres
//        "vx": -2.0, "vy": 0,               // m/s
//        "stop_after_s": 5.0}               // optional: halts there
//     ]
//   }

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "vision/lidar/cloud_source.hpp"

namespace gate::vision::lidar {

struct SceneObject {
    enum class Shape : std::uint8_t { kCar, kBox };
    Shape shape = Shape::kCar;
    float length = 4.5f, width = 1.75f, height = 1.45f;
    float x = 0.0f, y = 0.0f, yaw = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float stop_after_s = -1.0f;  // < 0: never stops
};

struct SceneConfig {
    Pose mount;
    float points_per_second = 21'600.0f;
    float rate_hz = 10.0f;
    float noise_m = 0.02f;
    float range_max = 30.0f;
    std::uint32_t seed = 1;
    float duration_s = 0.0f;
    bool realtime = false;
    std::vector<SceneObject> objects;
    // First batch stamp; default = wall clock at construction.
    std::optional<std::chrono::system_clock::time_point> start;
};

SceneConfig load_scene(const std::filesystem::path& path);

class SyntheticSource final : public CloudSource {
public:
    explicit SyntheticSource(SceneConfig cfg);

    std::optional<PointCloud> next() override;
    [[nodiscard]] std::string describe() const override;

    // The scene geometry at `t_s` seconds after the start (for tests).
    [[nodiscard]] std::vector<SceneObject> objects_at(float t_s) const;

private:
    SceneConfig cfg_;
    Transform tf_;
    std::mt19937 rng_;
    std::uint64_t batch_ = 0;
    std::chrono::system_clock::time_point t0_;
    std::chrono::steady_clock::time_point steady_t0_;
};

}  // namespace gate::vision::lidar
