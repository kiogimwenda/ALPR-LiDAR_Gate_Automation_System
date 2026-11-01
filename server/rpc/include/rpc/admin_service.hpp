// admin_service.hpp — gRPC AdminService implementation.
//
// Thin proxy from gRPC over the AllowlistStore. The store does the real
// work; this class adapts protobuf request/response shapes to the store's
// C++ API, validates inputs, and returns gRPC status codes.

#pragma once

#include <grpcpp/support/status.h>

#include "auth/allowlist_store.hpp"
#include "gate_service.grpc.pb.h"
#include "rpc/access_control.hpp"
#include "rpc/gate_dispatch.hpp"

namespace gate::rpc {

class AdminServiceImpl final : public gate::v1::AdminService::Service {
public:
    explicit AdminServiceImpl(gate::auth::AllowlistStore& store);

    grpc::Status UpsertAllowlist(grpc::ServerContext* ctx,
                                 const gate::v1::UpsertAllowlistRequest* req,
                                 gate::v1::UpsertAllowlistResponse* resp) override;

    grpc::Status ListAllowlist(grpc::ServerContext* ctx, const gate::v1::ListAllowlistRequest* req,
                               gate::v1::ListAllowlistResponse* resp) override;

    grpc::Status DeleteAllowlist(grpc::ServerContext* ctx,
                                 const gate::v1::DeleteAllowlistRequest* req,
                                 gate::v1::UpsertAllowlistResponse* resp) override;

    grpc::Status UpsertBlocklist(grpc::ServerContext* ctx,
                                 const gate::v1::UpsertBlocklistRequest* req,
                                 gate::v1::UpsertAllowlistResponse* resp) override;
    grpc::Status ListBlocklist(grpc::ServerContext* ctx, const gate::v1::ListBlocklistRequest* req,
                               gate::v1::ListBlocklistResponse* resp) override;
    grpc::Status DeleteBlocklist(grpc::ServerContext* ctx,
                                 const gate::v1::DeleteAllowlistRequest* req,
                                 gate::v1::UpsertAllowlistResponse* resp) override;
    grpc::Status SetVehicleAccess(grpc::ServerContext* ctx,
                                  const gate::v1::SetVehicleAccessRequest* req,
                                  gate::v1::VehicleAccess* resp) override;
    grpc::Status CreateGuestPass(grpc::ServerContext* ctx, const gate::v1::GuestPass* req,
                                 gate::v1::GuestPass* resp) override;
    grpc::Status ListGuestPasses(grpc::ServerContext* ctx,
                                 const gate::v1::ListGuestPassesRequest* req,
                                 gate::v1::ListGuestPassesResponse* resp) override;
    grpc::Status RevokeGuestPass(grpc::ServerContext* ctx,
                                 const gate::v1::RevokeGuestPassRequest* req,
                                 gate::v1::GuestPass* resp) override;

    grpc::Status GetGateConfig(grpc::ServerContext* ctx, const gate::v1::GetGateConfigRequest* req,
                               gate::v1::GateConfigRecord* resp) override;
    grpc::Status SetGateConfig(grpc::ServerContext* ctx, const gate::v1::SetGateConfigRequest* req,
                               gate::v1::GateConfigRecord* resp) override;
    void use_dispatch(GateDispatch* d) { dispatch_ = d; }

    // Per-RPC role checks from the mTLS client identity (access_control.hpp).
    void set_enforce_roles(bool on) { policy_.set_enforced(on); }

private:
    AccessPolicy policy_;
    gate::auth::AllowlistStore& store_;
    GateDispatch* dispatch_ = nullptr;
};

}  // namespace gate::rpc
