// lidar_config.cpp — fail-closed JSON → DetectorConfig.

#include "vision/lidar/lidar_config.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>

namespace gate::vision::lidar {

namespace {

using json = nlohmann::json;

[[noreturn]] void fail(const std::string& where, const std::string& why) {
    throw std::runtime_error("lidar config: " + where + ": " + why);
}

void only_keys(const json& j, const std::set<std::string>& allowed, const std::string& where) {
    if (!j.is_object()) {
        fail(where, "must be an object");
    }
    for (const auto& [k, v] : j.items()) {
        if (allowed.count(k) == 0) {
            fail(where, "unknown key '" + k + "'");
        }
    }
}

float f(const json& j, const char* key, float dflt, const std::string& where) {
    if (!j.contains(key)) {
        return dflt;
    }
    if (!j.at(key).is_number()) {
        fail(where + "." + key, "must be a number");
    }
    return j.at(key).get<float>();
}

std::uint32_t u(const json& j, const char* key, std::uint32_t dflt, const std::string& where) {
    if (!j.contains(key)) {
        return dflt;
    }
    if (!j.at(key).is_number_unsigned()) {
        fail(where + "." + key, "must be a non-negative integer");
    }
    return j.at(key).get<std::uint32_t>();
}

Zone zone(const json& j, const Zone& dflt, const std::string& where) {
    only_keys(j, {"x_min", "x_max", "y_min", "y_max", "z_min", "z_max"}, where);
    Zone z{f(j, "x_min", dflt.x_min, where), f(j, "x_max", dflt.x_max, where),
           f(j, "y_min", dflt.y_min, where), f(j, "y_max", dflt.y_max, where),
           f(j, "z_min", dflt.z_min, where), f(j, "z_max", dflt.z_max, where)};
    if (!(z.x_min < z.x_max && z.y_min < z.y_max && z.z_min < z.z_max)) {
        fail(where, "each *_min must be below its *_max");
    }
    return z;
}

}  // namespace

DetectorConfig load_detector_config(const std::filesystem::path& path) {
    std::ifstream in{path};
    if (!in) {
        fail(path.string(), "cannot open");
    }
    json j;
    try {
        in >> j;
    } catch (const json::exception& e) {
        fail(path.string(), e.what());
    }
    const std::string w = path.filename().string();
    only_keys(j,
              {"mount",
               "range_min",
               "range_max",
               "track_zone",
               "trigger_zone",
               "exclude_zones",
               "window_ms",
               "ground_clearance",
               "ground_search_max",
               "fit_ground_plane",
               "bev_cell",
               "cluster_gap_min",
               "cluster_gap_scale",
               "bridge_cells_max",
               "lane_aligned",
               "min_cluster_points",
               "min_cluster_height",
               "association_gate",
               "track_timeout_ms",
               "coast_ms",
               "confirm_hits"},
              w);
    DetectorConfig c;
    if (j.contains("mount")) {
        const auto& m = j.at("mount");
        only_keys(m, {"x", "y", "z", "roll", "pitch", "yaw"}, w + ".mount");
        c.mount = {f(m, "x", 0, w),    f(m, "y", 0, w),     f(m, "z", 0, w),
                   f(m, "roll", 0, w), f(m, "pitch", 0, w), f(m, "yaw", 0, w)};
    }
    c.range_min = f(j, "range_min", c.range_min, w);
    c.range_max = f(j, "range_max", c.range_max, w);
    if (!(c.range_min >= 0 && c.range_min < c.range_max)) {
        fail(w, "need 0 <= range_min < range_max");
    }
    if (j.contains("track_zone")) {
        c.track_zone = zone(j.at("track_zone"), c.track_zone, w + ".track_zone");
    }
    if (j.contains("trigger_zone")) {
        c.trigger_zone = zone(j.at("trigger_zone"), c.trigger_zone, w + ".trigger_zone");
    }
    if (j.contains("exclude_zones")) {
        if (!j.at("exclude_zones").is_array()) {
            fail(w + ".exclude_zones", "must be an array");
        }
        int i = 0;
        for (const auto& z : j.at("exclude_zones")) {
            c.exclude_zones.push_back(
                zone(z, Zone{}, w + ".exclude_zones[" + std::to_string(i++) + "]"));
        }
    }
    c.window = std::chrono::milliseconds{
        u(j, "window_ms", static_cast<std::uint32_t>(c.window.count()), w)};
    c.ground_clearance = f(j, "ground_clearance", c.ground_clearance, w);
    c.ground_search_max = f(j, "ground_search_max", c.ground_search_max, w);
    if (j.contains("fit_ground_plane")) {
        if (!j.at("fit_ground_plane").is_boolean()) {
            fail(w + ".fit_ground_plane", "must be true or false");
        }
        c.fit_ground_plane = j.at("fit_ground_plane").get<bool>();
    }
    c.bev_cell = f(j, "bev_cell", c.bev_cell, w);
    c.cluster_gap_min = f(j, "cluster_gap_min", c.cluster_gap_min, w);
    c.cluster_gap_scale = f(j, "cluster_gap_scale", c.cluster_gap_scale, w);
    c.bridge_cells_max = static_cast<int>(
        u(j, "bridge_cells_max", static_cast<std::uint32_t>(c.bridge_cells_max), w));
    if (j.contains("lane_aligned")) {
        if (!j.at("lane_aligned").is_boolean()) {
            fail(w + ".lane_aligned", "must be true or false");
        }
        c.lane_aligned = j.at("lane_aligned").get<bool>();
    }
    c.min_cluster_points = u(j, "min_cluster_points", c.min_cluster_points, w);
    c.min_cluster_height = f(j, "min_cluster_height", c.min_cluster_height, w);
    c.association_gate = f(j, "association_gate", c.association_gate, w);
    c.track_timeout = std::chrono::milliseconds{
        u(j, "track_timeout_ms", static_cast<std::uint32_t>(c.track_timeout.count()), w)};
    c.coast =
        std::chrono::milliseconds{u(j, "coast_ms", static_cast<std::uint32_t>(c.coast.count()), w)};
    c.confirm_hits = u(j, "confirm_hits", c.confirm_hits, w);
    if (c.window.count() <= 0 || c.bev_cell <= 0.01f || c.bridge_cells_max < 1 ||
        c.cluster_gap_min <= 0 || c.track_timeout.count() <= 0) {
        fail(w,
             "window_ms, bev_cell (> 0.01), cluster_gap_min, bridge_cells_max (>= 1), "
             "track_timeout_ms must be positive");
    }
    return c;
}

}  // namespace gate::vision::lidar
