// observation.hpp — plain-C++ per-tick sensor observations (Phase 5.2).
//
// One ObservationSet is what a sensing unit reports for a single capture
// tick: zero or more plate reads (ALPR) and zero or more vehicle
// detections (LiDAR / fusion unit), under one capture timestamp. The
// structs deliberately mirror the proto's PlateDetection /
// VehicleDetection field-for-field but stay protobuf-free, so future
// capture drivers (RTSP + TensorRT camera source, the Kyocera fusion
// unit) can be built without linking protobuf; FrameBuilder owns the
// one conversion into gate::v1::DetectionFrame.

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace gate::vision {

// Mirrors gate::v1::VehicleClass value-for-value so the FrameBuilder
// conversion is a static_cast-free explicit switch (kept in one place).
enum class VehicleClass : std::uint8_t {
    kUnknown = 0,
    kPedestrian,
    kBicycle,
    kMotorcycle,
    kSedan,
    kSuv,
    kPickup,
    kVan,
    kTruck,
};

// 2D pixel box in the camera frame (proto BoundingBox).
struct PixelBox {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// 3D oriented box relative to the LiDAR origin, ROS REP-103 axes,
// meters (proto BoundingBox3D + Point3D center).
struct OrientedBox3D {
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float length = 0.0f;  // X dimension
    float width = 0.0f;   // Y dimension
    float height = 0.0f;  // Z dimension
    float yaw_radians = 0.0f;
};

// One ALPR read: plate detection + OCR collapsed, like the proto's
// PlateDetection.
struct PlateObservation {
    std::string text;             // Normalized uppercase, no spaces
    float detection_conf = 0.0f;  // Plate-detector confidence [0, 1]
    float ocr_conf = 0.0f;        // Mean character confidence [0, 1]
    PixelBox box;                 // Plate location in the camera frame
    std::string country_hint;     // ISO-3166 alpha-2, "" if unknown
    std::string raw_ocr;          // Pre-normalization OCR text (audit)
};

// One vehicle detection from the ranging side (proto VehicleDetection).
struct VehicleObservation {
    VehicleClass vehicle_class = VehicleClass::kUnknown;
    float confidence = 0.0f;        // Classifier confidence [0, 1]
    OrientedBox3D box;              // Position + extent, meters
    std::uint32_t track_id = 0;     // Stable across frames, 0 = untracked
    std::uint32_t point_count = 0;  // LiDAR points in the cluster
};

// Everything one capture tick produced. `capture_ts` is the earliest
// sensor timestamp of the tick — it maps 1:1 onto DetectionFrame's
// capture_ts.
struct ObservationSet {
    std::chrono::system_clock::time_point capture_ts;
    std::vector<PlateObservation> plates;
    std::vector<VehicleObservation> vehicles;
};

}  // namespace gate::vision
