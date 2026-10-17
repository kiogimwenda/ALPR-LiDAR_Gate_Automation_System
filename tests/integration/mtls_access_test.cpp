// mtls_access_test.cpp — per-RPC roles over a real mTLS channel.
//
// Generates a throwaway site PKI with the shipped scripts
// (gen-tls-certs.sh + issue-gate-cert.sh), starts gate-server's Server
// with --require-client-cert semantics, and checks that each identity
// reaches exactly the RPCs its role allows.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <grpcpp/create_channel.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <memory>
#include <random>
#include <sstream>
#include <string>

#include "auth/allowlist_store.hpp"
#include "dash/event_broadcaster.hpp"
#include "fusion/fusion_engine.hpp"
#include "gate_service.grpc.pb.h"
#include "rpc/server.hpp"

#ifndef GATE_SOURCE_DIR
#error "GATE_SOURCE_DIR must point at the repository root"
#endif

namespace fs = std::filesystem;
using gate::v1::ControlEnvelope;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in{p, std::ios::binary};
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// One PKI for the whole binary — key generation is the slow part.
const fs::path& pki_dir() {
    static const fs::path dir = [] {
        std::mt19937_64 rng{std::random_device{}()};
        fs::path d = fs::temp_directory_path() / ("gate-mtls-" + std::to_string(rng()));
        const std::string src = GATE_SOURCE_DIR;
        const std::string cmd = "bash '" + src + "/scripts/gen-tls-certs.sh' '" + d.string() +
                                "' >/dev/null 2>&1 && bash '" + src +
                                "/scripts/issue-gate-cert.sh' '" + d.string() +
                                "' gate-01 >/dev/null 2>&1";
        REQUIRE(std::system(cmd.c_str()) == 0);
        std::atexit([] {
            std::error_code ec;
            fs::remove_all(pki_dir(), ec);
        });
        return d;
    }();
    return dir;
}

std::shared_ptr<grpc::Channel> channel_as(const std::string& addr, const std::string& leaf) {
    grpc::SslCredentialsOptions o;
    o.pem_root_certs = slurp(pki_dir() / "ca.pem");
    o.pem_cert_chain = slurp(pki_dir() / (leaf + ".pem"));
    o.pem_private_key = slurp(pki_dir() / (leaf + ".key"));
    grpc::ChannelArguments args;
    args.SetSslTargetNameOverride("localhost");
    return grpc::CreateCustomChannel(addr, grpc::SslCredentials(o), args);
}

struct MtlsServer {
    gate::auth::AllowlistStore store = gate::auth::AllowlistStore::open(":memory:");
    gate::fusion::FusionEngine fusion{store};
    gate::dash::EventBroadcaster bus;
    std::unique_ptr<gate::rpc::Server> server;

    MtlsServer() {
        gate::rpc::ServerConfig cfg;
        cfg.listen_address = "127.0.0.1:0";
        cfg.subscribe_poll_interval = std::chrono::milliseconds{50};
        cfg.tls.cert_path = (pki_dir() / "server.pem").string();
        cfg.tls.key_path = (pki_dir() / "server.key").string();
        cfg.tls.ca_path = (pki_dir() / "ca.pem").string();
        cfg.tls.require_client_cert = true;
        server = std::make_unique<gate::rpc::Server>(store, fusion, bus, cfg);
        REQUIRE(server->start());
    }
    ~MtlsServer() {
        bus.stop();
        server->shutdown(std::chrono::milliseconds{500});
    }
    std::string addr() const { return server->bound_address(); }
};

grpc::StatusCode list_allowlist(const std::shared_ptr<grpc::Channel>& ch) {
    auto stub = gate::v1::AdminService::NewStub(ch);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{5});
    gate::v1::ListAllowlistRequest req;
    req.set_site_id("default");
    gate::v1::ListAllowlistResponse resp;
    return stub->ListAllowlist(&ctx, req, &resp).error_code();
}

grpc::StatusCode submit_detection(const std::shared_ptr<grpc::Channel>& ch) {
    auto stub = gate::v1::FieldControllerService::NewStub(ch);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{5});
    gate::v1::DetectionFrame frame;
    frame.set_gate_id("gate-01");
    frame.add_plates()->set_plate_text("KDA123A");
    gate::v1::AuthDecision decision;
    return stub->SubmitDetection(&ctx, frame, &decision).error_code();
}

grpc::StatusCode issue_command(const std::shared_ptr<grpc::Channel>& ch) {
    auto stub = gate::v1::DashboardService::NewStub(ch);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{5});
    gate::v1::GateCommand cmd;
    cmd.set_gate_id("gate-01");
    cmd.set_kind(gate::v1::COMMAND_KIND_OPEN_GATE);
    gate::v1::CommandAck ack;
    return stub->IssueCommand(&ctx, cmd, &ack).error_code();
}

// Opens Control as `gate_id`; returns the stream's final status.
grpc::StatusCode control_as(const std::shared_ptr<grpc::Channel>& ch, const std::string& gate_id) {
    auto stub = gate::v1::FieldControllerService::NewStub(ch);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds{800});
    auto stream = stub->Control(&ctx);
    ControlEnvelope hello;
    hello.mutable_telemetry()->set_gate_id(gate_id);
    stream->Write(hello);
    stream->WritesDone();
    ControlEnvelope ignored;
    while (stream->Read(&ignored)) {
    }
    return stream->Finish().error_code();
}

}  // namespace

constexpr auto kDenied = grpc::StatusCode::PERMISSION_DENIED;

TEST_CASE("mTLS: the dashboard identity reaches Admin + Dashboard RPCs only",
          "[integration][mtls][access]") {
    MtlsServer s;
    const auto dash = channel_as(s.addr(), "dashboard");
    CHECK(list_allowlist(dash) == grpc::StatusCode::OK);
    CHECK(issue_command(dash) == grpc::StatusCode::OK);
    CHECK(submit_detection(dash) == kDenied);
    CHECK(control_as(dash, "gate-01") == kDenied);
}

TEST_CASE("mTLS: the vision identity may only submit detections", "[integration][mtls][access]") {
    MtlsServer s;
    const auto vision = channel_as(s.addr(), "vision");
    CHECK(submit_detection(vision) == grpc::StatusCode::OK);
    CHECK(list_allowlist(vision) == kDenied);
    CHECK(issue_command(vision) == kDenied);
    CHECK(control_as(vision, "gate-01") == kDenied);
}

TEST_CASE("mTLS: field identities cannot administer, command, or authorize",
          "[integration][mtls][access]") {
    MtlsServer s;
    for (const char* leaf : {"gate-client", "gate-01"}) {
        INFO(leaf);
        const auto field = channel_as(s.addr(), leaf);
        CHECK(list_allowlist(field) == kDenied);
        CHECK(issue_command(field) == kDenied);
        CHECK(submit_detection(field) == kDenied);
    }
}

TEST_CASE("mTLS: a gate-bound leaf may open Control only as its own gate",
          "[integration][mtls][access]") {
    MtlsServer s;
    const auto gate01 = channel_as(s.addr(), "gate-01");
    // Own gate: the stream is admitted (it ends at our deadline/half-close).
    CHECK(control_as(gate01, "gate-01") != kDenied);
    CHECK(control_as(gate01, "gate-02") == kDenied);
    // The shared bench leaf is not gate-bound.
    CHECK(control_as(channel_as(s.addr(), "gate-client"), "gate-02") != kDenied);
}
