// access_control.cpp — CN-based role checks for every RPC.

#include "rpc/access_control.hpp"

#include <grpcpp/security/auth_context.h>
#include <spdlog/spdlog.h>

namespace gate::rpc {

namespace {

constexpr std::string_view kFieldPrefix = "gate-field-";
constexpr std::string_view kSharedFieldCn = "gate-field-client";
constexpr std::string_view kVisionCn = "gate-vision";

}  // namespace

PeerIdentity identity_for_common_name(std::string_view cn) {
    PeerIdentity id;
    id.common_name = std::string{cn};
    if (cn == "gate-dashboard") {
        id.role = Role::kDashboard;
    } else if (cn == kVisionCn || (cn.size() > kVisionCn.size() + 1 &&
                                   cn.substr(0, kVisionCn.size() + 1) == "gate-vision-")) {
        id.role = Role::kVision;
    } else if (cn == kSharedFieldCn) {
        id.role = Role::kField;  // shared bench leaf — not gate-bound
    } else if (cn.size() > kFieldPrefix.size() &&
               cn.substr(0, kFieldPrefix.size()) == kFieldPrefix) {
        id.role = Role::kField;
        id.gate_id = std::string{cn.substr(kFieldPrefix.size())};
    }
    return id;
}

PeerIdentity peer_identity(const grpc::ServerContext* ctx) {
    if (ctx == nullptr) {
        return {};
    }
    const auto auth = ctx->auth_context();
    if (!auth || !auth->IsPeerAuthenticated()) {
        return {};
    }
    const auto cns = auth->FindPropertyValues("x509_common_name");
    if (cns.empty()) {
        return {};
    }
    return identity_for_common_name(std::string_view{cns.front().data(), cns.front().size()});
}

grpc::Status AccessPolicy::check(const grpc::ServerContext* ctx, std::uint8_t allowed,
                                 const char* rpc, PeerIdentity* who) const {
    if (!enforced_) {
        if (who != nullptr) {
            *who = peer_identity(ctx);
        }
        return grpc::Status::OK;
    }
    PeerIdentity id = peer_identity(ctx);
    if ((static_cast<std::uint8_t>(id.role) & allowed) == 0) {
        spdlog::warn("access denied: {} called by '{}'", rpc,
                     id.common_name.empty() ? "<no client certificate>" : id.common_name);
        return {grpc::StatusCode::PERMISSION_DENIED,
                std::string{rpc} + " is not permitted for client '" + id.common_name + "'"};
    }
    if (who != nullptr) {
        *who = std::move(id);
    }
    return grpc::Status::OK;
}

grpc::Status AccessPolicy::check_gate(const PeerIdentity& who, const std::string& gate_id) const {
    if (!enforced_ || who.gate_id.empty() || who.gate_id == gate_id) {
        return grpc::Status::OK;
    }
    spdlog::warn("access denied: '{}' tried to act as gate '{}'", who.common_name, gate_id);
    return {grpc::StatusCode::PERMISSION_DENIED,
            "client '" + who.common_name + "' may only act as gate '" + who.gate_id + "'"};
}

}  // namespace gate::rpc
