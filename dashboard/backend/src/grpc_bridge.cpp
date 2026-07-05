// grpc_bridge.cpp — channel lifecycle + the process-wide accessor.

#include "dash_api/grpc_bridge.hpp"

#include <ctime>
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

namespace {

constexpr auto kRpcDeadline = std::chrono::seconds(2);

grpc::ClientContext& with_deadline(grpc::ClientContext& ctx) {
    ctx.set_deadline(std::chrono::system_clock::now() + kRpcDeadline);
    return ctx;
}

GrpcBridge::RpcResult to_result(const grpc::Status& s) {
    return {s.error_code(), s.error_message()};
}

}  // namespace

GrpcBridge::RpcResult GrpcBridge::issue_command(gate::v1::GateCommand cmd,
                                                gate::v1::CommandAck& ack) {
    if (cmd.actor().empty()) {
        cmd.set_actor(cfg_.actor);
    }
    grpc::ClientContext ctx;
    return to_result(dash_stub_->IssueCommand(&with_deadline(ctx), cmd, &ack));
}

GrpcBridge::RpcResult GrpcBridge::list_allowlist(std::uint32_t page_size,
                                                 const std::string& page_token,
                                                 gate::v1::ListAllowlistResponse& out) {
    gate::v1::ListAllowlistRequest req;
    req.set_site_id(cfg_.site_id);
    req.set_page_size(page_size);
    req.set_page_token(page_token);
    grpc::ClientContext ctx;
    return to_result(admin_stub_->ListAllowlist(&with_deadline(ctx), req, &out));
}

GrpcBridge::RpcResult GrpcBridge::upsert_allowlist(gate::v1::UpsertAllowlistRequest req,
                                                   gate::v1::UpsertAllowlistResponse& out) {
    if (req.site_id().empty()) {
        req.set_site_id(cfg_.site_id);
    }
    for (auto& entry : *req.mutable_entries()) {
        if (entry.added_by().empty()) {
            entry.set_added_by(cfg_.actor);
        }
        if (!entry.has_added_ts()) {
            entry.mutable_added_ts()->set_seconds(std::time(nullptr));
        }
    }
    grpc::ClientContext ctx;
    return to_result(admin_stub_->UpsertAllowlist(&with_deadline(ctx), req, &out));
}

GrpcBridge::RpcResult GrpcBridge::delete_allowlist(const std::string& plate_text,
                                                   gate::v1::UpsertAllowlistResponse& out) {
    gate::v1::DeleteAllowlistRequest req;
    req.set_site_id(cfg_.site_id);
    req.add_plate_texts(plate_text);
    grpc::ClientContext ctx;
    return to_result(admin_stub_->DeleteAllowlist(&with_deadline(ctx), req, &out));
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
