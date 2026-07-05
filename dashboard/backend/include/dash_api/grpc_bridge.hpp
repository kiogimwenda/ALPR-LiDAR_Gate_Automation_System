// grpc_bridge.hpp — the dashboard backend's one gRPC connection.
//
// The Drogon side of the house (controllers, WebSocket) never touches
// gRPC directly; everything upstream goes through this bridge. One
// shared channel to the gate-server carries both stubs
// (DashboardService for events/commands, AdminService for allowlist
// CRUD) — gRPC multiplexes calls over a single HTTP/2 connection, so
// one channel is the right number.
//
// Drogon instantiates controllers reflectively, so they cannot take
// constructor arguments; the process-wide set_bridge()/bridge() pair
// (wired once in main, before app().run()) is the hand-off point.
// Phase 4.7.1 ships connectivity + health; 4.7.2 adds the command and
// allowlist calls; 4.7.3 adds the Subscribe consumer.

#pragma once

#include <chrono>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>

#include "gate_service.grpc.pb.h"

namespace gate::dash_api {

class GrpcBridge {
public:
    struct Config {
        std::string server = "127.0.0.1:50051";  // gate-server gRPC address
        std::string site_id = "site-01";
        std::string actor = "dashboard";  // audit tag on issued commands
    };

    explicit GrpcBridge(Config cfg);

    // True when the channel is READY (optionally waiting up to `wait`
    // for a connection attempt to land). Feeds /api/health's
    // "upstream" field; the backend itself stays up either way —
    // a dashboard that can say "server unreachable" beats a dead one.
    [[nodiscard]] bool upstream_ready(std::chrono::milliseconds wait) const;

    [[nodiscard]] const Config& config() const { return cfg_; }

private:
    Config cfg_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<gate::v1::DashboardService::Stub> dash_stub_;
    std::unique_ptr<gate::v1::AdminService::Stub> admin_stub_;
};

// Process-wide bridge accessor for reflectively-created controllers.
// set_bridge() must run before drogon::app().run(); bridge() returns
// nullptr before that (health then reports upstream=false).
void set_bridge(std::shared_ptr<GrpcBridge> b);
std::shared_ptr<GrpcBridge> bridge();

}  // namespace gate::dash_api
