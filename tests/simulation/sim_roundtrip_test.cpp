// sim_roundtrip_test.cpp — gate-sim against a mock server, end to end
// over real gRPC.
//
// The mock implements just enough FieldControllerService to play the
// server's half of the Control contract: read telemetry, issue one
// OPEN command, collect acks until the completion arrives. What's
// under test is the full simulator stack — VirtualGate physics,
// tracker completion, SimClient's stream pump and double-ack wiring —
// across a real HTTP/2 connection, which is exactly the surface the
// gate-server will see in Phase 4.6+ demos.

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "gate_service.grpc.pb.h"
#include "gate_sim/sim_client.hpp"
#include "gate_sim/virtual_gate.hpp"

namespace {

class MockControlService final : public gate::v1::FieldControllerService::Service {
public:
    grpc::Status Control(grpc::ServerContext* /*ctx*/,
                         grpc::ServerReaderWriter<gate::v1::ControlEnvelope,
                                                  gate::v1::ControlEnvelope>* stream) override {
        gate::v1::ControlEnvelope in;
        bool command_sent = false;
        while (stream->Read(&in)) {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (in.has_telemetry()) {
                telemetry_.push_back(in.telemetry());
                if (!command_sent) {
                    command_sent = true;
                    gate::v1::ControlEnvelope out;
                    auto* cmd = out.mutable_command();
                    cmd->set_command_id("mock-open-1");
                    cmd->set_gate_id(in.telemetry().gate_id());
                    cmd->set_kind(gate::v1::COMMAND_KIND_OPEN_GATE);
                    cmd->set_actor("mock-server");
                    stream->Write(out);
                }
            } else if (in.has_ack()) {
                acks_.push_back(in.ack());
                if (in.ack().completed()) {
                    done_.store(true);
                    break;  // server half-closes; the sim session ends
                }
            }
        }
        return grpc::Status::OK;
    }

    bool done() const { return done_.load(); }

    std::vector<gate::v1::Telemetry> telemetry() {
        const std::lock_guard<std::mutex> lock(mutex_);
        return telemetry_;
    }
    std::vector<gate::v1::CommandAck> acks() {
        const std::lock_guard<std::mutex> lock(mutex_);
        return acks_;
    }

private:
    std::mutex mutex_;
    std::vector<gate::v1::Telemetry> telemetry_;
    std::vector<gate::v1::CommandAck> acks_;
    std::atomic<bool> done_{false};
};

}  // namespace

TEST_CASE("gate-sim completes a server-issued open over a real gRPC stream") {
    MockControlService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    REQUIRE(server != nullptr);
    REQUIRE(port != 0);

    // Fast physics so the whole lifecycle fits in a second or two.
    gate::sim::VirtualGate::Config gate_cfg;
    gate_cfg.travel_ms = 300;
    gate_cfg.motor_timeout_ms = 5'000;
    gate::sim::VirtualGate gate(gate_cfg);
    gate.boot_closed();
    REQUIRE(gate.state() == gate::state_machine::State::Closed);

    gate::sim::SimClient::Config client_cfg;
    client_cfg.server = "127.0.0.1:" + std::to_string(port);
    client_cfg.gate_id = "gate-sim-test";
    client_cfg.telemetry_period_ms = 50;
    client_cfg.tick_ms = 10;

    std::atomic<bool> shutdown{false};
    gate::sim::SimClient client(client_cfg, gate);
    std::thread session([&] { (void)client.run_session(shutdown); });

    // Wait for the full exchange: telemetry → OPEN → received ack →
    // travel → completed ack.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!service.done() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    shutdown.store(true);
    session.join();
    server->Shutdown();

    REQUIRE(service.done());

    // Telemetry carried the contract fields.
    const auto telemetry = service.telemetry();
    REQUIRE(!telemetry.empty());
    CHECK(telemetry.front().gate_id() == "gate-sim-test");
    CHECK(telemetry.front().gate_state() == gate::v1::GATE_STATE_CLOSED);
    CHECK(telemetry.front().limit_closed());
    CHECK(telemetry.front().has_sent_ts());

    // Double-ack: received (completed=false) then completed success
    // with the terminal state.
    const auto acks = service.acks();
    REQUIRE(acks.size() == 2);
    CHECK(acks[0].command_id() == "mock-open-1");
    CHECK_FALSE(acks[0].completed());
    CHECK(acks[1].command_id() == "mock-open-1");
    CHECK(acks[1].completed());
    CHECK(acks[1].success());
    CHECK(acks[1].state_after() == gate::v1::GATE_STATE_OPEN);

    // The virtual gate physically arrived.
    CHECK(gate.state() == gate::state_machine::State::Open);
    CHECK(gate.limit_open());
}
