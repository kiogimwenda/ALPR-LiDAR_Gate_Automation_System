// lidar_sweep_test.cpp — classification accuracy sweep (hidden tag; run with "[.sweep]").
//
// 30 seeds × 5 reference vehicles × 4 ranges through the pillar-mounted
// L1 simulation; prints detection / correct-class counts. Not part of the
// default run (it takes a few seconds and asserts nothing): the numbers
// are quoted in docs/lidar.md.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>

#include "vision/lidar/synthetic_source.hpp"
#include "vision/lidar/vehicle_detector.hpp"
using namespace gate::vision::lidar;
using gate::vision::VehicleClass;
TEST_CASE("classification sweep", "[.sweep]") {
    const Pose pillar{0.0f, -2.0f, 1.2f, 0.0f, 1.92f, 0.0f};
    struct V {
        const char* name;
        SceneObject::Shape shape;
        float l, w, h;
        VehicleClass want;
    };
    const V vs[] = {
        {"sedan Axio", SceneObject::Shape::kCar, 4.4f, 1.70f, 1.46f, VehicleClass::kSedan},
        {"sedan Camry", SceneObject::Shape::kCar, 4.88f, 1.84f, 1.45f, VehicleClass::kSedan},
        {"SUV RAV4", SceneObject::Shape::kCar, 4.6f, 1.85f, 1.69f, VehicleClass::kSuv},
        {"SUV Prado", SceneObject::Shape::kCar, 4.84f, 1.89f, 1.85f, VehicleClass::kSuv},
        {"van Hiace", SceneObject::Shape::kBox, 5.38f, 1.88f, 2.28f, VehicleClass::kVan}};
    for (float range : {5.0f, 8.0f, 10.0f, 12.0f}) {
        for (const auto& v : vs) {
            int ok = 0, seen = 0;
            for (std::uint32_t seed = 1; seed <= 30; ++seed) {
                SceneConfig s;
                s.mount = pillar;
                s.realtime = false;
                s.seed = seed;
                s.start = std::chrono::system_clock::time_point{} + std::chrono::hours{1000};
                SceneObject o;
                o.shape = v.shape;
                o.length = v.l;
                o.width = v.w;
                o.height = v.h;
                o.x = range + v.l / 2;
                s.objects = {o};
                SyntheticSource src{s};
                DetectorConfig dc;
                dc.mount = pillar;
                dc.trigger_zone.x_max = 16.0f;
                VehicleDetector det{dc};
                std::vector<gate::vision::VehicleObservation> out;
                for (int i = 0; i < 20; ++i)
                    out = det.update(*src.next());  // 2 s
                if (!out.empty()) {
                    ++seen;
                    ok += out[0].vehicle_class == v.want;
                }
            }
            std::printf("front at %4.1f m  %-12s detected %2d/30  correct class %2d/30\n", range,
                        v.name, seen, ok);
        }
    }
}
