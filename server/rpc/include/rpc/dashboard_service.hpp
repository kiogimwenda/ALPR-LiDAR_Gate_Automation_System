// dashboard_service.hpp — gRPC DashboardService implementation.
//
// Three RPCs:
//   - Subscribe   (server-streaming): pumps the EventBroadcaster's stream
//                                     to the client until disconnect.
//   - IssueCommand (unary): records the dashboard-issued GateCommand on
//                           the broadcaster and returns a "received" ack.
//                           Firmware delivery happens in 4.3.5.
//   - Authorize   (unary):  calls the FusionEngine, publishes the decision,
//                           returns it.

#pragma once

#include <chrono>
#include <grpcpp/support/status.h>
#include <string>

#include "dash/event_broadcaster.hpp"
#include "fusion/fusion_engine.hpp"
#include "gate_service.grpc.pb.h"

namespace gate::rpc {

class DashboardServiceImpl final : public gate::v1::DashboardService::Service {
public:
    DashboardServiceImpl(gate::dash::EventBroadcaster& bus, gate::fusion::FusionEngine& fusion,
                         std::string default_site_id);

    grpc::Status Subscribe(grpc::ServerContext* ctx, const gate::v1::DashboardSubscription* req,
                           grpc::ServerWriter<gate::v1::DashboardEvent>* writer) override;

    grpc::Status IssueCommand(grpc::ServerContext* ctx, const gate::v1::GateCommand* req,
                              gate::v1::CommandAck* resp) override;

    grpc::Status Authorize(grpc::ServerContext* ctx, const gate::v1::AuthorizeRequest* req,
                           gate::v1::AuthDecision* resp) override;

    // How long Subscribe blocks per next() call before re-checking
    // whether the gRPC writer has been cancelled. Tests can shorten this.
    void set_subscribe_poll_interval(std::chrono::milliseconds dt);

private:
    gate::dash::EventBroadcaster& bus_;
    gate::fusion::FusionEngine& fusion_;
    std::string default_site_id_;
    std::chrono::milliseconds subscribe_poll_{500};
};

}  // namespace gate::rpc
