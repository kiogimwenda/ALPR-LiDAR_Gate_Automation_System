// admin_service.hpp — gRPC AdminService implementation.
//
// Thin proxy from gRPC over the AllowlistStore. The store does the real
// work; this class adapts protobuf request/response shapes to the store's
// C++ API, validates inputs, and returns gRPC status codes.

#pragma once

#include <grpcpp/support/status.h>

#include "auth/allowlist_store.hpp"
#include "gate_service.grpc.pb.h"

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

private:
    gate::auth::AllowlistStore& store_;
};

}  // namespace gate::rpc
