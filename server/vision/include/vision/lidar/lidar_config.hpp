// lidar_config.hpp — site LiDAR configuration (commissioning output).
//
// Where the sensor is and where vehicles count are site facts, so they
// live in a JSON file (/etc/gate/lidar.json), not in flags. Every field
// is optional and defaults to DetectorConfig's; parsing is fail-closed —
// an unknown key or a wrong type throws, because a typo'd zone silently
// falling back to a default would authorise against the wrong region.
//
//   {
//     "mount": {"x": 0, "y": -1.5, "z": 1.2, "roll": 0, "pitch": 1.92, "yaw": 0},
//     "range_min": 0.5, "range_max": 20,
//     "track_zone":   {"x_min": 0, "x_max": 20, "y_min": -5, "y_max": 5, "z_min": -0.5,
//     "z_max": 4.5}, "trigger_zone": {"x_min": 1, "x_max": 12, "y_min": -2.5, "y_max": 2.5,
//     "z_min": -0.5, "z_max": 4.5}, "exclude_zones": [{"x_min": ..., ...}], "window_ms": 400,
//     "ground_clearance": 0.15, "ground_search_max": 0.5, "fit_ground_plane": true,
//     "bev_cell": 0.2, "cluster_gap_min": 0.4, "cluster_gap_scale": 2.5, "bridge_cells_max": 5,
//     "lane_aligned": true,
//     "min_cluster_points": 12, "min_cluster_height": 0.4,
//     "association_gate": 1.5, "track_timeout_ms": 1200, "coast_ms": 500,
//     "confirm_hits": 2
//   }
//
// Angles are radians. See docs/lidar.md for mounting and zone setup.

#pragma once

#include <filesystem>

#include "vision/lidar/vehicle_detector.hpp"

namespace gate::vision::lidar {

DetectorConfig load_detector_config(const std::filesystem::path& path);

}  // namespace gate::vision::lidar
