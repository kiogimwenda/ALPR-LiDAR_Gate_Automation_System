// access_control.hpp — per-RPC authorization from the mTLS client identity.
//
// mTLS proves a caller holds *a* certificate from the site CA. It does
// not say what the caller may do. Without this layer, the field
// controller's certificate could call AdminService to edit the
// allowlist, or DashboardService.IssueCommand to open any gate.
//
// Roles come from the client certificate's subject CN, which is issued
// by scripts/gen-tls-certs.sh and scripts/issue-gate-cert.sh:
//
//   CN                    role        may call
//   gate-dashboard        kDashboard  DashboardService.*, AdminService.*
//   gate-vision[-*]       kVision     FieldControllerService.SubmitDetection
//   gate-field-<gate_id>  kField      FieldControllerService.Control /
//                                     DeliverOta / ReportOtaProgress,
//                                     only as <gate_id>
//   gate-field-client     kField      the same, any gate_id (the legacy
//                                     shared bench leaf)
//
// Anything else gets PERMISSION_DENIED. Enforcement is on when the
// server requires client certificates. Without mTLS there is no peer
// identity to check, so the plaintext and server-TLS-only dev postures
// stay open, as before; gate-server logs that posture loudly at startup.

#pragma once

#include <cstdint>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>
#include <string>
#include <string_view>

namespace gate::rpc {

enum class Role : std::uint8_t {
    kNone = 0,
    kField = 1u << 0,
    kVision = 1u << 1,
    kDashboard = 1u << 2,
};

constexpr std::uint8_t operator|(Role a, Role b) {
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

struct PeerIdentity {
    Role role = Role::kNone;
    std::string common_name;
    // Gate a kField identity is bound to; empty = any gate (shared leaf).
    std::string gate_id;
};

// Pure CN → identity mapping (unit-tested; no gRPC types).
PeerIdentity identity_for_common_name(std::string_view cn);

// The caller's identity from its TLS auth context; role kNone when the
// peer is unauthenticated (no client certificate).
PeerIdentity peer_identity(const grpc::ServerContext* ctx);

class AccessPolicy {
public:
    void set_enforced(bool on) { enforced_ = on; }
    [[nodiscard]] bool enforced() const { return enforced_; }

    // OK if the caller holds one of `allowed` roles (or enforcement is
    // off); PERMISSION_DENIED naming `rpc` otherwise. `who`, if given,
    // receives the caller's identity for further checks (gate binding).
    grpc::Status check(const grpc::ServerContext* ctx, std::uint8_t allowed, const char* rpc,
                       PeerIdentity* who = nullptr) const;
    grpc::Status check(const grpc::ServerContext* ctx, Role allowed, const char* rpc,
                       PeerIdentity* who = nullptr) const {
        return check(ctx, static_cast<std::uint8_t>(allowed), rpc, who);
    }

    // A gate-bound field identity may act only for its own gate.
    grpc::Status check_gate(const PeerIdentity& who, const std::string& gate_id) const;

private:
    bool enforced_ = false;
};

}  // namespace gate::rpc
