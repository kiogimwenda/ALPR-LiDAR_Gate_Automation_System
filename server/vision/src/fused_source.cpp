// fused_source.cpp — nearest-timestamp pairing of plates and LiDAR vehicles.

#include "vision/fused_source.hpp"

#include <spdlog/spdlog.h>
#include <utility>

namespace gate::vision {

FusedSource::FusedSource(std::unique_ptr<ObservationSource> primary,
                         std::shared_ptr<lidar::LidarWorker> lidar, Config cfg)
    : primary_(std::move(primary)), lidar_(std::move(lidar)), cfg_(cfg) {}

std::optional<ObservationSet> FusedSource::next() {
    if (std::string fault = lidar_->fault(); !fault.empty()) {
        failure_ = std::move(fault);
        return std::nullopt;
    }
    auto obs = primary_->next();
    if (!obs) {
        failure_ = primary_->failure();
        return std::nullopt;
    }
    if (!obs->vehicles.empty()) {
        if (!warned_replaced_) {
            spdlog::warn("fused source: primary source reports vehicles; the LiDAR replaces them");
            warned_replaced_ = true;
        }
        obs->vehicles.clear();
    }
    if (obs->plates.empty()) {
        // Vehicle-only / empty tick: nothing to authorise yet. Hand
        // back an empty set (the daemon skips it) rather than looping
        // here, so the daemon's shutdown check still runs every tick.
        obs->vehicles.clear();
        return obs;
    }
    ++counters_.frames;
    const auto snap = lidar_->latest();
    if (snap.valid) {
        const auto skew = snap.stamp > obs->capture_ts ? snap.stamp - obs->capture_ts
                                                       : obs->capture_ts - snap.stamp;
        if (skew <= cfg_.max_skew) {
            obs->vehicles = snap.vehicles;
            if (!obs->vehicles.empty()) {
                ++counters_.with_vehicles;
            }
        } else {
            ++counters_.skew_misses;
            spdlog::warn(
                "fused source: LiDAR snapshot {} ms from the camera capture (max {}) — "
                "frame sent without vehicles",
                std::chrono::duration_cast<std::chrono::milliseconds>(skew).count(),
                cfg_.max_skew.count());
        }
    }
    return obs;
}

}  // namespace gate::vision
