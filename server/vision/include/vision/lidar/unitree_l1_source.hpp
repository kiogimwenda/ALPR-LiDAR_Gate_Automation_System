// unitree_l1_source.hpp — Unitree 4D LiDAR L1 driver (Phase 6.1).
//
// Wraps Unitree's unilidar_sdk (BSD-3-Clause; a prebuilt static library
// for x86_64 and aarch64) behind CloudSource. The L1 talks over
// USB-serial at 2 Mbaud natively, or over UDP through Unitree's
// serial-to-UDP adapter board:
//
//   --lidar unitree-serial:/dev/ttyUSB0
//   --lidar unitree-udp:10.10.10.10:5001@10.10.10.100:5000
//                       (lidar ip:port @ local ip:port — SDK defaults)
//
// Built only when CMake finds the SDK (-DUNITREE_LIDAR_SDK_DIR=…/unitree_lidar_sdk);
// gate-vision then defines GATE_VISION_HAVE_UNITREE. The SDK's
// runParse() must be polled at ≥ 1.5 kHz, so next() spins on it with a
// 200 µs sleep while the line is idle, and returns each assembled
// point cloud (about 2,160 points, ~10 per second with the default 18
// scans per cloud). No cloud for `timeout` → stream ends with failure().
//
// Points arrive in the L1 frame (origin at the mounting face centre, +X
// away from the cable, +Z up) and are stamped with host receive time;
// the detector's mounting Pose moves them into the gate frame.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "vision/lidar/cloud_source.hpp"

namespace gate::vision::lidar {

class UnitreeL1Source final : public CloudSource {
public:
    struct Config {
        enum class Transport : std::uint8_t { kSerial, kUdp };
        Transport transport = Transport::kSerial;
        std::string serial_port = "/dev/ttyUSB0";
        std::uint32_t baudrate = 2'000'000;
        std::string lidar_ip = "10.10.10.10";
        std::uint16_t lidar_port = 5001;
        std::string local_ip = "10.10.10.100";
        std::uint16_t local_port = 5000;
        std::uint16_t cloud_scan_num = 18;
        float range_min = 0.0f;  // SDK-side range gate (metres)
        float range_max = 50.0f;
        std::chrono::milliseconds timeout{2000};
    };

    // Parses "unitree-serial:<port>" / "unitree-udp:<ip>:<port>@<ip>:<port>";
    // throws std::invalid_argument on anything else.
    static Config parse_spec(const std::string& spec);

    // Opens the port / socket; throws std::runtime_error on failure.
    explicit UnitreeL1Source(Config cfg);
    ~UnitreeL1Source() override;

    std::optional<PointCloud> next() override;
    [[nodiscard]] std::string failure() const override { return failure_; }
    [[nodiscard]] std::string describe() const override;

private:
    struct Impl;
    Config cfg_;
    std::unique_ptr<Impl> impl_;
    std::string failure_;
};

}  // namespace gate::vision::lidar
