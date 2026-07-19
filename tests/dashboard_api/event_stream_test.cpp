// event_stream_test.cpp — the Subscribe consumer against a mock
// server: live ingest, death, and gap-free resume via since_event_id.

#include "dash_api/event_stream.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <mutex>
#include <thread>
#include <vector>

#include "gate_service.grpc.pb.h"

namespace dash = gate::dash_api;

namespace {

// First subscription: writes events 1..3 and ends the stream (server
// restart stand-in). Second subscription: records since_event_id,
// writes 4..5, then holds until cancelled.
class MockDashboardService final : public gate::v1::DashboardService::Service {
public:
    grpc::Status Subscribe(grpc::ServerContext* ctx, const gate::v1::DashboardSubscription* req,
                           grpc::ServerWriter<gate::v1::DashboardEvent>* writer) override {
        const int session = ++sessions_;
        if (session == 2) {
            resume_since_.store(req->since_event_id());
        }
        const std::uint64_t base = (session == 1) ? 1 : 4;
        const std::uint64_t count = (session == 1) ? 3 : 2;
        for (std::uint64_t i = 0; i < count; ++i) {
            gate::v1::DashboardEvent ev;
            ev.set_event_id(base + i);
            ev.mutable_telemetry()->set_gate_id("gate-01");
            ev.mutable_telemetry()->set_gate_state(gate::v1::GATE_STATE_CLOSED);
            writer->Write(ev);
        }
        if (session == 1) {
            return grpc::Status::OK;  // die; the consumer must resume
        }
        while (!ctx->IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return grpc::Status::OK;
    }

    std::atomic<int> sessions_{0};
    std::atomic<std::uint64_t> resume_since_{0};
};

}  // namespace

TEST_CASE("event stream ingests, survives a server drop, and resumes without a gap") {
    MockDashboardService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    REQUIRE(server != nullptr);

    dash::GrpcBridge::Config bridge_cfg;
    bridge_cfg.server = "127.0.0.1:" + std::to_string(port);
    bridge_cfg.site_id = "site-test";
    bridge_cfg.actor = "test";
    auto bridge = std::make_shared<dash::GrpcBridge>(bridge_cfg);
    dash::EventStream::Config cfg;
    cfg.reconnect_min_ms = 50;  // fast test reconnect
    dash::EventStream stream(bridge, cfg);
    stream.start();

    // Wait until both sessions ran and all five events landed.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((service.sessions_.load() < 2 || stream.cache().last_event_id() < 5) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    stream.stop();
    server->Shutdown();

    CHECK(service.sessions_.load() >= 2);
    CHECK(stream.cache().last_event_id() == 5);
    // The reconnect asked to resume exactly after what it had.
    CHECK(service.resume_since_.load() == 3);
    // All five frames are in the ring for new-connection replay.
    CHECK(stream.cache().recent_frames().size() == 5);
}
