// lidar_bench.cpp — VehicleDetector latency vs the documented budget.
//
// Ray-casts a gate scene (a sedan driving up and stopping, a pedestrian
// beside it, an SUV queued behind) at the requested point rate and
// times VehicleDetector::update() per batch. Prints min / p50 / p99 / max
// and exits non-zero if p99 exceeds --budget-ms (default 20, the
// docs/lidar.md requirement). Build Release for meaningful numbers.
//
//   lidar-bench [--points-per-second 21600] [--window-ms 400] [--frames 300]

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "vision/lidar/synthetic_source.hpp"
#include "vision/lidar/vehicle_detector.hpp"

using namespace gate::vision::lidar;

int main(int argc, char** argv) {
    float pps = 21'600.0f;
    int frames = 300;
    int window_ms = 400;
    double budget_ms = 20.0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--points-per-second")) {
            pps = std::strtof(argv[i + 1], nullptr);
        } else if (!std::strcmp(argv[i], "--frames")) {
            frames = static_cast<int>(std::strtol(argv[i + 1], nullptr, 10));
        } else if (!std::strcmp(argv[i], "--window-ms")) {
            window_ms = static_cast<int>(std::strtol(argv[i + 1], nullptr, 10));
        } else if (!std::strcmp(argv[i], "--budget-ms")) {
            budget_ms = std::strtod(argv[i + 1], nullptr);
        } else {
            std::fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }

    SceneConfig scene;
    scene.mount = {0.0f, -1.5f, 1.2f, 0.0f, 1.92f, 0.0f};  // pillar mount, 20° down
    scene.points_per_second = pps;
    scene.realtime = false;
    scene.objects = {
        {SceneObject::Shape::kCar, 4.4f, 1.7f, 1.45f, 14.0f, 0.0f, 0.0f, -2.0f, 0.0f, 4.5f},
        {SceneObject::Shape::kBox, 0.5f, 0.5f, 1.75f, 5.0f, 1.6f, 0.0f, 0.0f, 0.0f, -1.0f},
        {SceneObject::Shape::kCar, 4.6f, 1.85f, 1.75f, 19.0f, 0.3f, 0.0f, -1.5f, 0.0f, 4.0f},
    };
    SyntheticSource src{scene};

    DetectorConfig cfg;
    cfg.mount = scene.mount;
    cfg.window = std::chrono::milliseconds{window_ms};
    VehicleDetector det{cfg};

    std::vector<double> ms;
    std::size_t max_window = 0;
    for (int f = 0; f < frames; ++f) {
        const auto pc = src.next();
        if (!pc) {
            break;
        }
        det.update(*pc);
        ms.push_back(det.stats().update_ms);
        max_window = std::max(max_window, det.stats().window_points);
    }
    std::sort(ms.begin(), ms.end());
    const auto pct = [&](double p) {
        return ms[static_cast<std::size_t>(p * static_cast<double>(ms.size() - 1))];
    };
    std::printf("points/s %.0f, window %d ms (max %zu points), %d frames\n", pps, window_ms,
                max_window, frames);
    std::printf("update(): min %.2f  p50 %.2f  p99 %.2f  max %.2f ms  (budget p99 <= %.1f ms)\n",
                ms.front(), pct(0.5), pct(0.99), ms.back(), budget_ms);
    for (const auto& v : det.current()) {
        std::printf("  track %u: class %d conf %.2f at (%.1f, %.1f) %.2f x %.2f x %.2f m, %u pts\n",
                    v.track_id, static_cast<int>(v.vehicle_class), v.confidence, v.box.center_x,
                    v.box.center_y, v.box.length, v.box.width, v.box.height, v.point_count);
    }
    return pct(0.99) <= budget_ms ? 0 : 1;
}
