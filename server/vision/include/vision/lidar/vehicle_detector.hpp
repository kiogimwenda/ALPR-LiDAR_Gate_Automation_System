// vehicle_detector.hpp — point clouds → tracked, classified vehicles (Phase 6.1).
//
// The LiDAR half of the fusion input, following
// docs/diagrams/03-lidar-subsystem.mmd:
//
//   1. transform each batch sensor → gate frame (site mounting Pose),
//      range-gate it in the sensor frame, crop to the tracking zone,
//      drop commissioning-masked regions (exclude_zones);
//   2. accumulate a short sliding window — the Unitree L1 delivers about
//      21,600 points/s over a 360° × 90° dome, far too sparse for one
//      batch to outline a car;
//   3. fit the ground plane (RANSAC on low points, EMA-smoothed) and
//      keep points more than ground_clearance above it;
//   4. cluster in a bird's-eye grid (vehicles in a lane separate in
//      XY, and the grid makes this O(points)). The gap bridged between
//      cells grows with range, because the sensor's point spacing does:
//      gap = max(cluster_gap_min, cluster_gap_scale · range · angular
//      spacing), where the angular spacing comes from the window's
//      actual point count. A sparse car at 10 m stays in one piece, and
//      a person standing 0.5 m from a car at the gate stays separate;
//   5. fit a box per cluster — lane-aligned by default (vehicles at a
//      gate drive along the lane, and a pillar-mounted sensor sees an
//      L-shaped front + side + roof whose PCA heading is meaningless),
//      PCA heading otherwise — and measure length / width / height
//      above ground;
//   6. track clusters across ticks by their *near face* (the side
//      nearest the gate is the one a pillar sensor sees best; the far
//      end appears and disappears with occlusion, which would make a
//      centroid wander) — greedy nearest-neighbour gated by predicted
//      motion, alpha-beta velocity filter. A track's dimensions grow
//      fast and shrink slowly, so a partial view never shrinks a car,
//      and motion smear from the window is subtracted along the heading.
//      The reported box extends from the near face by the tracked length;
//   7. classify each track from its dimensions against per-class
//      envelopes (classifier below) and report the confirmed tracks
//      whose centre lies in the trigger zone, nearest the gate first.
//
// Performance requirement (docs/lidar.md): update() ≤ 20 ms for a 25k-point
// window on one CPU core, so a 5 Hz camera tick never waits on the
// LiDAR. tests/vision/lidar_detector_test.cpp checks the
// classification requirements on ray-cast scenes; lidar-bench measures
// the latency.
//
// Not thread-safe: one detector per LiDAR, driven by one thread
// (LidarWorker).

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>

#include "vision/lidar/point_cloud.hpp"
#include "vision/observation.hpp"

namespace gate::vision::lidar {

struct DetectorConfig {
    Pose mount;  // sensor pose in the gate frame

    // Sensor-frame range gate (L1 PM: 0.05–20 m usable for this job).
    float range_min = 0.5f;
    float range_max = 20.0f;

    // Everything the tracker sees, in the gate frame.
    Zone track_zone{0.0f, 20.0f, -5.0f, 5.0f, -0.5f, 4.5f};
    // Vehicles are reported only while their box centre is in here.
    Zone trigger_zone{1.0f, 12.0f, -2.5f, 2.5f, -0.5f, 4.5f};
    // Regions that never hold a vehicle (pillars, planters, the gate
    // leaf's travel) — set during commissioning.
    std::vector<Zone> exclude_zones;

    std::chrono::milliseconds window{400};  // accumulation window

    // Ground.
    float ground_clearance = 0.15f;  // points this far above ground count
    float ground_search_max = 0.5f;  // gate-frame z below which points may be ground
    bool fit_ground_plane = true;

    // Bird's-eye clustering.
    float bev_cell = 0.20f;
    float cluster_gap_min = 0.4f;    // metres bridged at short range
    float cluster_gap_scale = 2.5f;  // × expected point spacing at range
    int bridge_cells_max = 5;        // cap on the bridged gap, in cells
    bool lane_aligned = true;        // boxes along +X (see step 5)
    std::uint32_t min_cluster_points = 12;
    float min_cluster_height = 0.4f;

    // Tracking.
    float association_gate = 1.5f;  // metres, plus predicted motion
    std::chrono::milliseconds track_timeout{1200};
    // Keep reporting a track this long after its last sighting — a
    // person half-hidden behind a car drops below min_cluster_points for
    // a tick now and then.
    std::chrono::milliseconds coast{500};
    std::uint32_t confirm_hits = 2;
};

// Per-class envelope classifier on (length, width, height) in metres.
struct ClassScores {
    std::array<float, 9> p{};  // indexed by VehicleClass; normalised
    VehicleClass best = VehicleClass::kUnknown;
    float best_p = 0.0f;
    float raw = 0.0f;  // un-normalised score of `best` (0..1): how well it fits at all
};
ClassScores classify_dimensions(float length, float width, float height);

class VehicleDetector {
public:
    explicit VehicleDetector(DetectorConfig cfg);

    // Ingest one driver batch, re-run the pipeline over the window, and
    // return the vehicles in the trigger zone (nearest the gate first).
    std::vector<VehicleObservation> update(const PointCloud& batch);

    // The latest report, unchanged since the last update().
    [[nodiscard]] const std::vector<VehicleObservation>& current() const { return report_; }

    struct Stats {
        std::size_t window_points = 0;
        std::size_t object_points = 0;  // above ground
        std::size_t clusters = 0;
        std::size_t tracks = 0;
        float ground_a = 0.0f, ground_b = 0.0f, ground_c = 0.0f;  // z = a·x + b·y + c
        double update_ms = 0.0;
    };
    [[nodiscard]] const Stats& stats() const { return stats_; }
    [[nodiscard]] const DetectorConfig& config() const { return cfg_; }

    // Cluster geometry as measured in one tick (exposed for tests).
    struct Cluster {
        float cx = 0, cy = 0, cz = 0;  // box centre (gate frame)
        float length = 0, width = 0, height = 0;
        float yaw = 0;
        float near_x = 0;  // smallest gate-frame x: the face nearest the gate
        std::uint32_t points = 0;
    };
    [[nodiscard]] const std::vector<Cluster>& last_clusters() const { return clusters_; }

private:
    struct Stamped {
        std::chrono::system_clock::time_point stamp;
        std::vector<Point> pts;  // gate frame, cropped
        std::size_t raw = 0;     // points the driver delivered
    };
    struct Track {
        std::uint32_t id = 0;
        float x = 0, y = 0, vx = 0, vy = 0;  // x = near face, y = footprint centre
        float length = 0, width = 0, height = 0, yaw = 0, cz = 0;
        std::uint32_t hits = 0;
        std::uint32_t points = 0;
        std::chrono::system_clock::time_point last_seen{};
        bool updated = false;
    };

    void fit_ground();
    void cluster();
    void track(std::chrono::system_clock::time_point now);
    void report(std::chrono::system_clock::time_point now);

    DetectorConfig cfg_;
    Transform tf_;
    std::deque<Stamped> window_;
    std::vector<Point> scratch_;  // window points, flattened
    std::size_t window_raw_ = 0;  // raw (pre-crop) points in the window
    std::vector<float> height_;   // per scratch point, above ground
    float ga_ = 0.0f, gb_ = 0.0f, gc_ = 0.0f;
    bool ground_init_ = false;
    std::uint32_t rng_ = 0x9E3779B9u;
    std::vector<Cluster> clusters_;
    std::vector<Track> tracks_;
    std::uint32_t next_track_id_ = 1;
    std::vector<VehicleObservation> report_;
    Stats stats_;
};

}  // namespace gate::vision::lidar
