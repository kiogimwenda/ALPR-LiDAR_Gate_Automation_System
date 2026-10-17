// lidar_worker.hpp — runs a CloudSource + VehicleDetector on its own thread.
//
// The LiDAR streams at its own rate (L1: ~10 batches/s) independent of
// the camera's inference cadence. The worker drains the driver
// continuously so its buffers never back up, feeds every batch to the
// detector, and publishes the latest vehicle report as a timestamped
// Snapshot that FusedSource pairs with each camera frame.
//
// Health is explicit: fault() turns non-empty when the driver's stream
// ends or no batch has arrived for `stale_after`. FusedSource then ends
// the observation stream, gate-vision exits non-zero, and systemd
// restarts it (re-opening the serial port / UDP socket). A dead LiDAR
// must not look like an empty driveway.

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "vision/lidar/cloud_source.hpp"
#include "vision/lidar/vehicle_detector.hpp"

namespace gate::vision::lidar {

class LidarWorker {
public:
    struct Config {
        std::chrono::milliseconds stale_after{1500};
        // If set, every raw batch is also written here as NNNNNNNN.pcd
        // (sensor frame) for later replay with `--lidar pcd:<dir>`.
        std::filesystem::path record_dir;
    };

    struct Snapshot {
        bool valid = false;  // false until the first batch is processed
        std::chrono::system_clock::time_point stamp{};
        std::vector<VehicleObservation> vehicles;
        VehicleDetector::Stats stats;
    };

    LidarWorker(std::unique_ptr<CloudSource> source, VehicleDetector detector, Config cfg);
    ~LidarWorker();
    LidarWorker(const LidarWorker&) = delete;
    LidarWorker& operator=(const LidarWorker&) = delete;

    // Starts the background thread (idempotent).
    void start();
    // Stops and joins it.
    void stop();

    // Pull and process exactly one batch on the caller's thread; false at
    // end of stream. For tests and the bench tool (don't mix with start()).
    bool step();

    [[nodiscard]] Snapshot latest() const;
    // Empty while healthy; else why the LiDAR is unusable.
    [[nodiscard]] std::string fault() const;
    [[nodiscard]] std::string describe() const { return source_->describe(); }

private:
    void run();

    std::unique_ptr<CloudSource> source_;
    VehicleDetector detector_;
    Config cfg_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> started_{false};

    mutable std::mutex mu_;
    Snapshot snap_;
    std::string ended_;  // set when the stream ends
    std::chrono::steady_clock::time_point last_batch_;
    std::chrono::steady_clock::time_point created_;
    std::uint64_t recorded_ = 0;
};

}  // namespace gate::vision::lidar
