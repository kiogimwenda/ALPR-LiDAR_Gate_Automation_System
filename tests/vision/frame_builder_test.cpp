// frame_builder_test.cpp — faithful ObservationSet → DetectionFrame mapping.
//
// The builder is the one place plain observations become wire protobuf,
// so these tests pin every field crossing that boundary plus the two
// contract guarantees: frame_id is monotonic per builder, and capture_ts
// survives the chrono → protobuf Timestamp split at nanosecond grain.

#include "vision/frame_builder.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>

using gate::vision::FrameBuilder;
using gate::vision::FrameBuilderConfig;
using gate::vision::ObservationSet;
using gate::vision::PlateObservation;
using gate::vision::VehicleClass;
using gate::vision::VehicleObservation;

namespace {

FrameBuilder make_builder() {
    return FrameBuilder{FrameBuilderConfig{"site-a", "gate-01", ""}};
}

ObservationSet full_observation() {
    ObservationSet obs;
    obs.capture_ts = std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000} +
                                                           std::chrono::nanoseconds{123'456'789}};

    PlateObservation p;
    p.text = "KDA123X";
    p.detection_conf = 0.94f;
    p.ocr_conf = 0.91f;
    p.box = {100, 220, 180, 60};
    p.country_hint = "KE";
    p.raw_ocr = "KDA 123X";
    obs.plates.push_back(p);

    VehicleObservation v;
    v.vehicle_class = VehicleClass::kSedan;
    v.confidence = 0.88f;
    v.box = {4.5f, -0.3f, 0.8f, 4.6f, 1.8f, 1.5f, 0.05f};
    v.track_id = 7;
    v.point_count = 412;
    obs.vehicles.push_back(v);
    return obs;
}

}  // namespace

TEST_CASE("frame builder maps every field onto the proto", "[vision][frame_builder]") {
    auto builder = make_builder();
    const auto frame = builder.build(full_observation());

    CHECK(frame.gate_id() == "gate-01");
    CHECK(frame.frame_id() == 1);
    CHECK(frame.capture_ts().seconds() == 1'700'000'000);
    CHECK(frame.capture_ts().nanos() == 123'456'789);

    REQUIRE(frame.plates_size() == 1);
    const auto& p = frame.plates(0);
    CHECK(p.plate_text() == "KDA123X");
    CHECK(p.detection_conf() == 0.94f);
    CHECK(p.ocr_conf() == 0.91f);
    CHECK(p.box().x() == 100);
    CHECK(p.box().y() == 220);
    CHECK(p.box().width() == 180);
    CHECK(p.box().height() == 60);
    CHECK(p.country_hint() == "KE");
    CHECK(p.raw_ocr() == "KDA 123X");

    REQUIRE(frame.vehicles_size() == 1);
    const auto& v = frame.vehicles(0);
    CHECK(v.vehicle_class() == gate::v1::VEHICLE_CLASS_SEDAN);
    CHECK(v.class_conf() == 0.88f);
    CHECK(v.box().center().x() == 4.5f);
    CHECK(v.box().center().y() == -0.3f);
    CHECK(v.box().center().z() == 0.8f);
    CHECK(v.box().length() == 4.6f);
    CHECK(v.box().width() == 1.8f);
    CHECK(v.box().height() == 1.5f);
    CHECK(v.box().yaw_radians() == 0.05f);
    CHECK(v.track_id() == 7);
    CHECK(v.point_count() == 412);
}

TEST_CASE("frame ids are monotonic per builder", "[vision][frame_builder]") {
    auto builder = make_builder();
    CHECK(builder.last_frame_id() == 0);  // "never assigned" sentinel

    const ObservationSet obs = full_observation();
    CHECK(builder.build(obs).frame_id() == 1);
    CHECK(builder.build(obs).frame_id() == 2);
    CHECK(builder.build(obs).frame_id() == 3);
    CHECK(builder.last_frame_id() == 3);

    // Independent builders own independent sequences — the proto's
    // contract is monotonic per gate_id, and one builder serves one gate.
    auto other = make_builder();
    CHECK(other.build(obs).frame_id() == 1);
}

TEST_CASE("empty observation sets build empty-but-stamped frames", "[vision][frame_builder]") {
    auto builder = make_builder();
    ObservationSet obs;
    obs.capture_ts = std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}};

    const auto frame = builder.build(obs);
    CHECK(frame.plates_size() == 0);
    CHECK(frame.vehicles_size() == 0);
    CHECK(frame.gate_id() == "gate-01");
    CHECK(frame.frame_id() == 1);
    CHECK(frame.capture_ts().seconds() == 1'700'000'000);
    CHECK(frame.capture_ts().nanos() == 0);
}

TEST_CASE("every vehicle class maps onto its proto twin", "[vision][frame_builder]") {
    using gate::vision::to_proto_class;
    CHECK(to_proto_class(VehicleClass::kUnknown) == gate::v1::VEHICLE_CLASS_UNKNOWN);
    CHECK(to_proto_class(VehicleClass::kPedestrian) == gate::v1::VEHICLE_CLASS_PEDESTRIAN);
    CHECK(to_proto_class(VehicleClass::kBicycle) == gate::v1::VEHICLE_CLASS_BICYCLE);
    CHECK(to_proto_class(VehicleClass::kMotorcycle) == gate::v1::VEHICLE_CLASS_MOTORCYCLE);
    CHECK(to_proto_class(VehicleClass::kSedan) == gate::v1::VEHICLE_CLASS_SEDAN);
    CHECK(to_proto_class(VehicleClass::kSuv) == gate::v1::VEHICLE_CLASS_SUV);
    CHECK(to_proto_class(VehicleClass::kPickup) == gate::v1::VEHICLE_CLASS_PICKUP);
    CHECK(to_proto_class(VehicleClass::kVan) == gate::v1::VEHICLE_CLASS_VAN);
    CHECK(to_proto_class(VehicleClass::kTruck) == gate::v1::VEHICLE_CLASS_TRUCK);
}
