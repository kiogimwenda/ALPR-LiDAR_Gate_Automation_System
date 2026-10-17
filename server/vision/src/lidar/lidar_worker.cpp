// lidar_worker.cpp — background driver pump + detector.

#include "vision/lidar/lidar_worker.hpp"

#include <cstdio>
#include <spdlog/spdlog.h>
#include <utility>

#include "vision/lidar/pcd_source.hpp"

namespace gate::vision::lidar {

LidarWorker::LidarWorker(std::unique_ptr<CloudSource> source, VehicleDetector detector, Config cfg)
    : source_(std::move(source)), detector_(std::move(detector)), cfg_(std::move(cfg)) {
    created_ = last_batch_ = std::chrono::steady_clock::now();
    if (!cfg_.record_dir.empty()) {
        std::filesystem::create_directories(cfg_.record_dir);
    }
}

LidarWorker::~LidarWorker() {
    stop();
}

void LidarWorker::start() {
    if (started_.exchange(true)) {
        return;
    }
    thread_ = std::thread([this] { run(); });
}

void LidarWorker::stop() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void LidarWorker::run() {
    while (!stop_.load() && step()) {
    }
}

bool LidarWorker::step() {
    auto batch = source_->next();
    if (!batch) {
        const std::string why = source_->failure();
        const std::lock_guard lk{mu_};
        ended_ = "LiDAR stream ended (" + source_->describe() + ")" +
                 (why.empty() ? std::string{} : ": " + why);
        spdlog::error("lidar: {}", ended_);
        return false;
    }
    if (!cfg_.record_dir.empty()) {
        char name[32];
        std::snprintf(name, sizeof(name), "%08llu.pcd",
                      static_cast<unsigned long long>(recorded_++));
        try {
            write_pcd(cfg_.record_dir / name, batch->points);
        } catch (const std::exception& e) {
            spdlog::warn("lidar: recording stopped: {}", e.what());
            cfg_.record_dir.clear();
        }
    }
    auto vehicles = detector_.update(*batch);
    const auto& st = detector_.stats();
    if (st.update_ms > 20.0) {
        // The documented budget (docs/lidar.md); log, don't fail.
        spdlog::warn("lidar: detector update took {:.1f} ms ({} window points)", st.update_ms,
                     st.window_points);
    }
    const std::lock_guard lk{mu_};
    snap_.valid = true;
    snap_.stamp = batch->stamp;
    snap_.vehicles = std::move(vehicles);
    snap_.stats = st;
    last_batch_ = std::chrono::steady_clock::now();
    return true;
}

LidarWorker::Snapshot LidarWorker::latest() const {
    const std::lock_guard lk{mu_};
    return snap_;
}

std::string LidarWorker::fault() const {
    const std::lock_guard lk{mu_};
    if (!ended_.empty()) {
        return ended_;
    }
    const auto silent = std::chrono::steady_clock::now() - last_batch_;
    if (silent > cfg_.stale_after) {
        return "no LiDAR data for " +
               std::to_string(
                   std::chrono::duration_cast<std::chrono::milliseconds>(silent).count()) +
               " ms (" + source_->describe() + ")";
    }
    return {};
}

}  // namespace gate::vision::lidar
