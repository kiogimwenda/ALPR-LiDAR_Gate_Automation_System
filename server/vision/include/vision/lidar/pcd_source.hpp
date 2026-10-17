// pcd_source.hpp — PCD read/write + recorded-cloud replay (Phase 6.1).
//
// Replay is one of the three simulation modes (Phase 1 decision 10):
// record the L1 at a site (`gate-vision --lidar ... --record-lidar
// <dir>`), then replay the directory through the same detector with
// `--lidar pcd:<dir>`.
//
// Reads the Point Cloud Library's .pcd v0.7 format — DATA ascii or
// binary (not binary_compressed), FIELDS including x y z and optionally
// intensity, float32 (TYPE F SIZE 4) or float64 (TYPE F SIZE 8) — so
// clouds exported from ROS / CloudCompare / the Unitree tools load too.
// Writes binary PCD with x y z intensity.

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "vision/lidar/cloud_source.hpp"

namespace gate::vision::lidar {

// Throws std::runtime_error naming the file on any format problem.
std::vector<Point> read_pcd(const std::filesystem::path& path);
void write_pcd(const std::filesystem::path& path, const std::vector<Point>& points);

class PcdReplaySource final : public CloudSource {
public:
    struct Config {
        std::filesystem::path dir;  // *.pcd, replayed in filename order
        float rate_hz = 10.0f;
        bool realtime = true;    // pace batches; false = as fast as pulled
        std::uint32_t loop = 1;  // passes over the directory (0 = forever)
    };

    // Lists the directory; throws if it holds no .pcd files.
    explicit PcdReplaySource(Config cfg);

    std::optional<PointCloud> next() override;
    [[nodiscard]] std::string failure() const override { return failure_; }
    [[nodiscard]] std::string describe() const override;

private:
    Config cfg_;
    std::vector<std::filesystem::path> files_;
    std::size_t index_ = 0;
    std::uint32_t pass_ = 0;
    std::uint64_t emitted_ = 0;
    std::chrono::steady_clock::time_point t0_;
    std::chrono::system_clock::time_point sys_t0_;
    std::string failure_;
};

}  // namespace gate::vision::lidar
