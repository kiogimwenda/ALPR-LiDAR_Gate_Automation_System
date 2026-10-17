// fused_source.hpp — camera (or scenario) plates + LiDAR vehicles in one frame.
//
// The discrete-sensor fusion of ADR-006/007: the primary source (the
// RTSP camera, or a scripted scenario on a bench) paces the stream and
// supplies plate reads; on every tick that has a plate, the LiDAR
// worker's latest vehicle report is attached if it is within
// `max_skew` of the camera capture. That pairing by nearest timestamp
// is exactly what ADR-012 notes a hardware-synchronised fusion unit
// will later make exact.
//
// Vehicle-only ticks are not emitted (a car with no plate read yet would
// just generate NO_PLATE denials at camera rate); the next plate read
// carries it. If the primary source already reports vehicles they are
// replaced: with a LiDAR configured, it is the only vehicle authority.
//
// Fail-closed: a LiDAR fault ends the stream with failure() set, so
// gate-vision exits non-zero and systemd restarts it.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "vision/lidar/lidar_worker.hpp"
#include "vision/source.hpp"

namespace gate::vision {

class FusedSource final : public ObservationSource {
public:
    struct Config {
        std::chrono::milliseconds max_skew{300};
    };

    FusedSource(std::unique_ptr<ObservationSource> primary,
                std::shared_ptr<lidar::LidarWorker> lidar, Config cfg);

    std::optional<ObservationSet> next() override;
    [[nodiscard]] std::string failure() const override { return failure_; }

    struct Counters {
        std::uint64_t frames = 0;         // plate frames emitted
        std::uint64_t with_vehicles = 0;  // ... that carried LiDAR vehicles
        std::uint64_t skew_misses = 0;    // LiDAR snapshot too far from capture
    };
    [[nodiscard]] const Counters& counters() const { return counters_; }

private:
    std::unique_ptr<ObservationSource> primary_;
    std::shared_ptr<lidar::LidarWorker> lidar_;
    Config cfg_;
    std::string failure_;
    Counters counters_;
    bool warned_replaced_ = false;
};

}  // namespace gate::vision
