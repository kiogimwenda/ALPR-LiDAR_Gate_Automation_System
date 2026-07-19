// frame_builder.cpp — the one plain-C++ → protobuf conversion.

#include "vision/frame_builder.hpp"

#include <chrono>

namespace gate::vision {

namespace {

// system_clock time_point → protobuf Timestamp (seconds + nanos),
// same split the sim client uses for telemetry stamps.
void stamp(google::protobuf::Timestamp* ts, std::chrono::system_clock::time_point tp) {
    const auto since_epoch = tp.time_since_epoch();
    ts->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(since_epoch).count());
    ts->set_nanos(static_cast<std::int32_t>(
        (std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch) %
         std::chrono::seconds(1))
            .count()));
}

}  // namespace

gate::v1::VehicleClass to_proto_class(VehicleClass cls) {
    switch (cls) {
        case VehicleClass::kPedestrian:
            return gate::v1::VEHICLE_CLASS_PEDESTRIAN;
        case VehicleClass::kBicycle:
            return gate::v1::VEHICLE_CLASS_BICYCLE;
        case VehicleClass::kMotorcycle:
            return gate::v1::VEHICLE_CLASS_MOTORCYCLE;
        case VehicleClass::kSedan:
            return gate::v1::VEHICLE_CLASS_SEDAN;
        case VehicleClass::kSuv:
            return gate::v1::VEHICLE_CLASS_SUV;
        case VehicleClass::kPickup:
            return gate::v1::VEHICLE_CLASS_PICKUP;
        case VehicleClass::kVan:
            return gate::v1::VEHICLE_CLASS_VAN;
        case VehicleClass::kTruck:
            return gate::v1::VEHICLE_CLASS_TRUCK;
        case VehicleClass::kUnknown:
        default:
            return gate::v1::VEHICLE_CLASS_UNKNOWN;
    }
}

gate::v1::DetectionFrame FrameBuilder::build(const ObservationSet& obs) {
    gate::v1::DetectionFrame frame;
    frame.set_frame_id(next_frame_id_++);
    frame.set_gate_id(cfg_.gate_id);
    stamp(frame.mutable_capture_ts(), obs.capture_ts);

    for (const PlateObservation& p : obs.plates) {
        gate::v1::PlateDetection* out = frame.add_plates();
        out->set_plate_text(p.text);
        out->set_detection_conf(p.detection_conf);
        out->set_ocr_conf(p.ocr_conf);
        out->mutable_box()->set_x(p.box.x);
        out->mutable_box()->set_y(p.box.y);
        out->mutable_box()->set_width(p.box.width);
        out->mutable_box()->set_height(p.box.height);
        out->set_country_hint(p.country_hint);
        out->set_raw_ocr(p.raw_ocr);
    }

    for (const VehicleObservation& v : obs.vehicles) {
        gate::v1::VehicleDetection* out = frame.add_vehicles();
        out->set_vehicle_class(to_proto_class(v.vehicle_class));
        out->set_class_conf(v.confidence);
        gate::v1::BoundingBox3D* box = out->mutable_box();
        box->mutable_center()->set_x(v.box.center_x);
        box->mutable_center()->set_y(v.box.center_y);
        box->mutable_center()->set_z(v.box.center_z);
        box->set_length(v.box.length);
        box->set_width(v.box.width);
        box->set_height(v.box.height);
        box->set_yaw_radians(v.box.yaw_radians);
        out->set_track_id(v.track_id);
        out->set_point_count(v.point_count);
    }

    return frame;
}

}  // namespace gate::vision
