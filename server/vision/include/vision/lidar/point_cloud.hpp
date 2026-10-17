// point_cloud.hpp — plain point-cloud types for the LiDAR path (Phase 6.1).
//
// Every LiDAR driver (Unitree L1 over serial/UDP, PCD replay, the
// synthetic ray-caster) yields PointCloud batches in the *sensor* frame;
// VehicleDetector moves them into the *gate* frame with the site's
// mounting Pose. No PCL, no Eigen — the hot path is a few thousand
// points per tick, and plain structs keep the CPU-only build lean.
//
// Frames (both right-handed, metres, ROS REP-103 style):
//   sensor — as the driver reports it. Unitree L1: origin at the centre
//            of the mounting face, +X away from the cable, +Z up.
//   gate   — origin on the ground at the gate centreline, +X out along
//            the approach lane (towards arriving vehicles), +Y to the
//            left looking out, +Z up. Trigger zones are expressed here.

#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gate::vision::lidar {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float intensity = 0.0f;
};

// One driver batch. `stamp` is the host receive time of the batch (the
// L1's own clock is not synchronised to the host); points within a batch
// are treated as simultaneous.
struct PointCloud {
    std::chrono::system_clock::time_point stamp{};
    std::vector<Point> points;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Rigid transform sensor → gate: rotate by roll (X), pitch (Y), yaw (Z),
// applied in that order (R = Rz·Ry·Rx), then translate. A sensor 2.5 m
// up on the pillar, tilted 20° down towards the lane, is
// {x=0, y=-1.2, z=2.5, roll=0, pitch=+0.349, yaw=0}.
struct Pose {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float roll = 0.0f;   // radians
    float pitch = 0.0f;  // radians
    float yaw = 0.0f;    // radians
};

// Precomputed rotation matrix + translation for a Pose.
class Transform {
public:
    Transform() = default;
    explicit Transform(const Pose& p) {
        const float cr = std::cos(p.roll), sr = std::sin(p.roll);
        const float cp = std::cos(p.pitch), sp = std::sin(p.pitch);
        const float cy = std::cos(p.yaw), sy = std::sin(p.yaw);
        r_[0] = cy * cp;
        r_[1] = cy * sp * sr - sy * cr;
        r_[2] = cy * sp * cr + sy * sr;
        r_[3] = sy * cp;
        r_[4] = sy * sp * sr + cy * cr;
        r_[5] = sy * sp * cr - cy * sr;
        r_[6] = -sp;
        r_[7] = cp * sr;
        r_[8] = cp * cr;
        t_ = {p.x, p.y, p.z};
    }

    [[nodiscard]] Vec3 apply(float x, float y, float z) const {
        return {r_[0] * x + r_[1] * y + r_[2] * z + t_.x, r_[3] * x + r_[4] * y + r_[5] * z + t_.y,
                r_[6] * x + r_[7] * y + r_[8] * z + t_.z};
    }
    // Rotation only (directions).
    [[nodiscard]] Vec3 rotate(float x, float y, float z) const {
        return {r_[0] * x + r_[1] * y + r_[2] * z, r_[3] * x + r_[4] * y + r_[5] * z,
                r_[6] * x + r_[7] * y + r_[8] * z};
    }
    // Inverse rotation (gate direction → sensor direction).
    [[nodiscard]] Vec3 rotate_inverse(float x, float y, float z) const {
        return {r_[0] * x + r_[3] * y + r_[6] * z, r_[1] * x + r_[4] * y + r_[7] * z,
                r_[2] * x + r_[5] * y + r_[8] * z};
    }
    [[nodiscard]] const Vec3& translation() const { return t_; }

private:
    float r_[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    Vec3 t_{};
};

// Axis-aligned region in the gate frame.
struct Zone {
    float x_min = 0.0f, x_max = 0.0f;
    float y_min = 0.0f, y_max = 0.0f;
    float z_min = 0.0f, z_max = 0.0f;

    [[nodiscard]] bool contains(float x, float y, float z) const {
        return x >= x_min && x <= x_max && y >= y_min && y <= y_max && z >= z_min && z <= z_max;
    }
    [[nodiscard]] bool contains_xy(float x, float y) const {
        return x >= x_min && x <= x_max && y >= y_min && y <= y_max;
    }
};

}  // namespace gate::vision::lidar
