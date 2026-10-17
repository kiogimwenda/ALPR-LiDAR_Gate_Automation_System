// unitree_l1_source.cpp — unilidar_sdk → CloudSource.

#include "vision/lidar/unitree_l1_source.hpp"

#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>
#include <unitree_lidar_sdk.h>

namespace gate::vision::lidar {

namespace usdk = unitree_lidar_sdk;

struct UnitreeL1Source::Impl {
    // The SDK class declares no virtual destructor, so deleting it
    // through the base pointer is undefined; the reader lives for the
    // process (one per daemon) and the OS reclaims it.
    usdk::UnitreeLidarReader* reader = nullptr;
};

namespace {

std::uint16_t parse_port(const std::string& s, const std::string& spec) {
    try {
        std::size_t used = 0;
        const unsigned long v = std::stoul(s, &used);
        if (used == s.size() && v > 0 && v <= 65535) {
            return static_cast<std::uint16_t>(v);
        }
    } catch (const std::exception&) {
    }
    throw std::invalid_argument("bad port '" + s + "' in " + spec);
}

// "ip:port" → (ip, port)
void split_endpoint(const std::string& ep, const std::string& spec, std::string& ip,
                    std::uint16_t& port) {
    const auto colon = ep.rfind(':');
    if (colon == std::string::npos || colon == 0) {
        throw std::invalid_argument("expected ip:port, got '" + ep + "' in " + spec);
    }
    ip = ep.substr(0, colon);
    port = parse_port(ep.substr(colon + 1), spec);
}

}  // namespace

UnitreeL1Source::Config UnitreeL1Source::parse_spec(const std::string& spec) {
    Config c;
    if (spec.rfind("unitree-serial:", 0) == 0) {
        c.transport = Config::Transport::kSerial;
        c.serial_port = spec.substr(std::string{"unitree-serial:"}.size());
        if (c.serial_port.empty()) {
            throw std::invalid_argument("unitree-serial: needs a device path");
        }
        return c;
    }
    if (spec.rfind("unitree-udp:", 0) == 0) {
        c.transport = Config::Transport::kUdp;
        const std::string rest = spec.substr(std::string{"unitree-udp:"}.size());
        const auto at = rest.find('@');
        if (at == std::string::npos) {
            throw std::invalid_argument(
                "unitree-udp: expected <lidar-ip>:<port>@<local-ip>:<port>, got " + spec);
        }
        split_endpoint(rest.substr(0, at), spec, c.lidar_ip, c.lidar_port);
        split_endpoint(rest.substr(at + 1), spec, c.local_ip, c.local_port);
        return c;
    }
    throw std::invalid_argument("not a unitree spec: " + spec);
}

UnitreeL1Source::UnitreeL1Source(Config cfg)
    : cfg_(std::move(cfg)), impl_(std::make_unique<Impl>()) {
    impl_->reader = usdk::createUnitreeLidarReader();
    if (impl_->reader == nullptr) {
        throw std::runtime_error("unitree-l1: createUnitreeLidarReader failed");
    }
    int rc = 0;
    if (cfg_.transport == Config::Transport::kSerial) {
        rc = impl_->reader->initialize(cfg_.cloud_scan_num, cfg_.serial_port, cfg_.baudrate,
                                       /*rotate_yaw_bias=*/0.0f, /*range_scale=*/0.001f,
                                       /*range_bias=*/0.0f, cfg_.range_max, cfg_.range_min);
    } else {
        rc = impl_->reader->initializeUDP(cfg_.cloud_scan_num, cfg_.lidar_port, cfg_.lidar_ip,
                                          cfg_.local_port, cfg_.local_ip, 0.0f, 0.001f, 0.0f,
                                          cfg_.range_max, cfg_.range_min);
    }
    if (rc != 0) {
        throw std::runtime_error("unitree-l1: cannot open " + describe() +
                                 " (serial: is the user in the dialout group?)");
    }
    // Wake it if a previous run left it in standby.
    impl_->reader->setLidarWorkingMode(usdk::NORMAL);
    spdlog::info("unitree-l1: opened {} (sdk {})", describe(), impl_->reader->getVersionOfSDK());
}

UnitreeL1Source::~UnitreeL1Source() {
    if (impl_ && impl_->reader != nullptr && cfg_.transport == Config::Transport::kUdp) {
        impl_->reader->closeUDP();
    }
}

std::optional<PointCloud> UnitreeL1Source::next() {
    const auto deadline = std::chrono::steady_clock::now() + cfg_.timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        switch (impl_->reader->runParse()) {
            case usdk::POINTCLOUD: {
                const usdk::PointCloudUnitree& c = impl_->reader->getCloud();
                PointCloud pc;
                pc.stamp = std::chrono::system_clock::now();
                pc.points.reserve(c.points.size());
                for (const auto& p : c.points) {
                    pc.points.push_back({p.x, p.y, p.z, p.intensity});
                }
                return pc;
            }
            case usdk::NONE:
                // Idle line: keep well above the SDK's 1.5 kHz poll floor.
                std::this_thread::sleep_for(std::chrono::microseconds{200});
                break;
            default:
                break;  // IMU / range / version / time-sync frames
        }
    }
    failure_ = "no point cloud from " + describe() + " for " +
               std::to_string(cfg_.timeout.count()) + " ms";
    return std::nullopt;
}

std::string UnitreeL1Source::describe() const {
    if (cfg_.transport == Config::Transport::kSerial) {
        return "unitree-l1 serial " + cfg_.serial_port;
    }
    return "unitree-l1 udp " + cfg_.lidar_ip + ":" + std::to_string(cfg_.lidar_port) + " via " +
           cfg_.local_ip + ":" + std::to_string(cfg_.local_port);
}

}  // namespace gate::vision::lidar
