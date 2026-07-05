// grpc_bridge.cpp — channel lifecycle + the process-wide accessor.

#include "dash_api/grpc_bridge.hpp"

#include <mutex>
#include <utility>

namespace gate::dash_api {

namespace {

std::mutex g_bridge_mutex;
std::shared_ptr<GrpcBridge> g_bridge;

}  // namespace

GrpcBridge::GrpcBridge(Config cfg) : cfg_(std::move(cfg)) {
    // Insecure on the trusted field LAN, matching the server listener;
    // TLS lands with the Phase 4.8 deployment work alongside the
    // firmware's (ADR-011 note).
    channel_ = grpc::CreateChannel(cfg_.server, grpc::InsecureChannelCredentials());
    dash_stub_ = gate::v1::DashboardService::NewStub(channel_);
    admin_stub_ = gate::v1::AdminService::NewStub(channel_);
}

bool GrpcBridge::upstream_ready(std::chrono::milliseconds wait) const {
    if (wait.count() > 0) {
        return channel_->WaitForConnected(std::chrono::system_clock::now() + wait);
    }
    return channel_->GetState(/*try_to_connect=*/true) == GRPC_CHANNEL_READY;
}

void set_bridge(std::shared_ptr<GrpcBridge> b) {
    const std::lock_guard<std::mutex> lock(g_bridge_mutex);
    g_bridge = std::move(b);
}

std::shared_ptr<GrpcBridge> bridge() {
    const std::lock_guard<std::mutex> lock(g_bridge_mutex);
    return g_bridge;
}

}  // namespace gate::dash_api
