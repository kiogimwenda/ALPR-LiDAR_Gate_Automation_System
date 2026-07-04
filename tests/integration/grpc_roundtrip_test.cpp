// grpc_roundtrip_test.cpp — end-to-end gRPC integration tests.
//
// Spins up a real gate::rpc::Server on an ephemeral local port, builds
// real gRPC client stubs against the resolved address, and exercises
// each service end-to-end:
//
//   - AdminService            — allowlist CRUD over the wire
//   - DashboardService        — Authorize, IssueCommand, Subscribe
//   - FieldControllerService  — SubmitDetection, bidi Control roundtrip

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <grpcpp/create_channel.h>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <thread>
#include <vector>

#include "auth/allowlist_store.hpp"
#include "dash/event_broadcaster.hpp"
#include "fusion/fusion_engine.hpp"
#include "gate_service.grpc.pb.h"
#include "rpc/server.hpp"

using gate::auth::AllowlistStore;
using gate::dash::EventBroadcaster;
using gate::fusion::FusionEngine;
using gate::rpc::Server;
using gate::rpc::ServerConfig;

using gate::v1::AdminService;
using gate::v1::Allowlistentry;
using gate::v1::AuthDecision;
using gate::v1::AuthorizeRequest;
using gate::v1::AuthVerdict;
using gate::v1::CommandAck;
using gate::v1::CommandKind;
using gate::v1::ControlEnvelope;
using gate::v1::DashboardEvent;
using gate::v1::DashboardService;
using gate::v1::DashboardSubscription;
using gate::v1::DeleteAllowlistRequest;
using gate::v1::DetectionFrame;
using gate::v1::FieldControllerService;
using gate::v1::GateCommand;
using gate::v1::ListAllowlistRequest;
using gate::v1::ListAllowlistResponse;
using gate::v1::PlateDetection;
using gate::v1::Telemetry;
using gate::v1::UpsertAllowlistRequest;
using gate::v1::UpsertAllowlistResponse;
using gate::v1::VehicleClass;
using gate::v1::VehicleDetection;

namespace {

// Test fixture: real Server on 127.0.0.1:0, real gRPC channel against it.
// One per TEST_CASE — Catch2 builds a fresh stack frame each time.
class ServerHarness {
public:
    ServerHarness()
        : store_(AllowlistStore::open(":memory:")),
          fusion_(store_),
          server_(store_, fusion_, bus_, makeConfig()) {
        REQUIRE(server_.start());
        channel_ = grpc::CreateChannel(server_.bound_address(), grpc::InsecureChannelCredentials());
    }

    ~ServerHarness() {
        bus_.stop();
        server_.shutdown(std::chrono::milliseconds{500});
    }

    AllowlistStore& store() { return store_; }
    FusionEngine& fusion() { return fusion_; }
    EventBroadcaster& bus() { return bus_; }
    std::shared_ptr<grpc::Channel> channel() { return channel_; }

    std::unique_ptr<AdminService::Stub> admin_stub() { return AdminService::NewStub(channel_); }
    std::unique_ptr<DashboardService::Stub> dashboard_stub() {
        return DashboardService::NewStub(channel_);
    }
    std::unique_ptr<FieldControllerService::Stub> field_stub() {
        return FieldControllerService::NewStub(channel_);
    }

private:
    static ServerConfig makeConfig() {
        ServerConfig cfg;
        cfg.listen_address = "127.0.0.1:0";
        // Tight poll so Subscribe's cancellation check fires before any
        // test's deadline.
        cfg.subscribe_poll_interval = std::chrono::milliseconds{50};
        return cfg;
    }

    AllowlistStore store_;
    FusionEngine fusion_;
    EventBroadcaster bus_;
    Server server_;
    std::shared_ptr<grpc::Channel> channel_;
};

}  // namespace

TEST_CASE("integration: AdminService Upsert + List + Delete round-trip over gRPC",
          "[integration][admin]") {
    ServerHarness h;
    auto stub = h.admin_stub();

    // Upsert two entries.
    {
        UpsertAllowlistRequest req;
        req.set_site_id("site-a");
        req.add_entries()->set_plate_text("KBZ-001A");
        req.add_entries()->set_plate_text("KBZ-002B");
        UpsertAllowlistResponse resp;
        grpc::ClientContext ctx;
        REQUIRE(stub->UpsertAllowlist(&ctx, req, &resp).ok());
        REQUIRE(resp.inserted() == 2);
    }
    // List them back.
    {
        ListAllowlistRequest req;
        req.set_site_id("site-a");
        req.set_page_size(10);
        ListAllowlistResponse resp;
        grpc::ClientContext ctx;
        REQUIRE(stub->ListAllowlist(&ctx, req, &resp).ok());
        REQUIRE(resp.entries_size() == 2);
        REQUIRE(resp.entries(0).plate_text() == "KBZ001A");
        REQUIRE(resp.entries(1).plate_text() == "KBZ002B");
    }
    // Delete one.
    {
        DeleteAllowlistRequest req;
        req.set_site_id("site-a");
        req.add_plate_texts("KBZ-001A");
        UpsertAllowlistResponse resp;
        grpc::ClientContext ctx;
        REQUIRE(stub->DeleteAllowlist(&ctx, req, &resp).ok());
        REQUIRE(resp.updated() == 1);
    }
    // List again.
    {
        ListAllowlistRequest req;
        req.set_site_id("site-a");
        ListAllowlistResponse resp;
        grpc::ClientContext ctx;
        REQUIRE(stub->ListAllowlist(&ctx, req, &resp).ok());
        REQUIRE(resp.entries_size() == 1);
        REQUIRE(resp.entries(0).plate_text() == "KBZ002B");
    }
}

TEST_CASE("integration: DashboardService::Authorize end-to-end with allowlist hit",
          "[integration][dashboard][authorize]") {
    ServerHarness h;
    // Seed the store directly so the test isn't gated on Admin's wire.
    {
        std::vector<Allowlistentry> seed;
        Allowlistentry e;
        e.set_plate_text("KBZ123A");
        seed.push_back(e);
        h.store().upsert("site-a", seed);
    }

    auto stub = h.dashboard_stub();
    AuthorizeRequest req;
    req.set_site_id("site-a");
    req.set_actor("auto");
    auto* f = req.mutable_frame();
    f->set_gate_id("gate-north");
    auto* p = f->add_plates();
    p->set_plate_text("KBZ-123A");
    p->set_detection_conf(0.9f);
    p->set_ocr_conf(0.85f);
    auto* v = f->add_vehicles();
    v->set_vehicle_class(VehicleClass::VEHICLE_CLASS_SEDAN);
    v->set_class_conf(0.95f);

    AuthDecision resp;
    grpc::ClientContext ctx;
    REQUIRE(stub->Authorize(&ctx, req, &resp).ok());
    REQUIRE(resp.verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);
    REQUIRE(resp.matched_plate() == "KBZ123A");
    REQUIRE_FALSE(resp.decision_id().empty());
}

TEST_CASE("integration: DashboardService::Subscribe streams live events",
          "[integration][dashboard][subscribe]") {
    ServerHarness h;
    auto stub = h.dashboard_stub();

    DashboardSubscription req;  // defaulted = everything

    grpc::ClientContext ctx;
    auto reader = stub->Subscribe(&ctx, req);

    // Drive a few events from the server side.
    h.bus().publish_telemetry("site-a", [] {
        Telemetry t;
        t.set_gate_id("gate-north");
        t.set_uptime_sec(1);
        return t;
    }());
    h.bus().publish_telemetry("site-a", [] {
        Telemetry t;
        t.set_gate_id("gate-north");
        t.set_uptime_sec(2);
        return t;
    }());
    h.bus().publish_telemetry("site-a", [] {
        Telemetry t;
        t.set_gate_id("gate-north");
        t.set_uptime_sec(3);
        return t;
    }());

    // Read back three events. Read() blocks until a message arrives or
    // the stream closes; bus.stop() in the harness destructor closes it.
    std::vector<DashboardEvent> received;
    for (int i = 0; i < 3; ++i) {
        DashboardEvent ev;
        REQUIRE(reader->Read(&ev));
        received.push_back(std::move(ev));
    }

    ctx.TryCancel();
    reader->Finish();  // status doesn't matter once we cancelled.

    REQUIRE(received.size() == 3);
    REQUIRE(received[0].telemetry().uptime_sec() == 1);
    REQUIRE(received[1].telemetry().uptime_sec() == 2);
    REQUIRE(received[2].telemetry().uptime_sec() == 3);
    REQUIRE(received[0].event_id() < received[1].event_id());
    REQUIRE(received[1].event_id() < received[2].event_id());
}

TEST_CASE("integration: FieldControllerService::SubmitDetection over gRPC",
          "[integration][field][submit]") {
    ServerHarness h;
    {
        std::vector<Allowlistentry> seed;
        Allowlistentry e;
        e.set_plate_text("KBZ123A");
        seed.push_back(e);
        h.store().upsert("default", seed);
    }
    auto stub = h.field_stub();

    DetectionFrame frame;
    frame.set_gate_id("gate-north");
    frame.set_frame_id(99);
    auto* p = frame.add_plates();
    p->set_plate_text("KBZ123A");
    p->set_detection_conf(0.92f);
    p->set_ocr_conf(0.88f);
    auto* v = frame.add_vehicles();
    v->set_vehicle_class(VehicleClass::VEHICLE_CLASS_SEDAN);
    v->set_class_conf(0.95f);

    AuthDecision resp;
    grpc::ClientContext ctx;
    REQUIRE(stub->SubmitDetection(&ctx, frame, &resp).ok());
    REQUIRE(resp.verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);
    REQUIRE(resp.actor() == "firmware-submit");
    REQUIRE(resp.frame_id() == 99);
}

TEST_CASE("integration: FieldControllerService::Control bidi stream forwards command",
          "[integration][field][control]") {
    ServerHarness h;
    auto stub = h.field_stub();

    grpc::ClientContext ctx;
    auto stream = stub->Control(&ctx);

    // Firmware identifies itself with a Telemetry first.
    {
        ControlEnvelope env;
        auto* t = env.mutable_telemetry();
        t->set_gate_id("gate-north");
        t->set_uptime_sec(0);
        REQUIRE(stream->Write(env));
    }

    // Wait briefly for the server to register the subscription, then
    // publish a command targeted at this gate.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    GateCommand cmd;
    cmd.set_gate_id("gate-north");
    cmd.set_command_id("cmd-test-1");
    cmd.set_kind(CommandKind::COMMAND_KIND_OPEN_GATE);
    h.bus().publish_command("default", cmd);

    // Read from the bidi stream — should receive the forwarded command.
    ControlEnvelope received;
    REQUIRE(stream->Read(&received));
    REQUIRE(received.payload_case() == ControlEnvelope::kCommand);
    REQUIRE(received.command().command_id() == "cmd-test-1");
    REQUIRE(received.command().gate_id() == "gate-north");

    // Firmware sends back an ack — should appear on a Dashboard
    // subscription (proves the reader-side dispatch works).
    auto dash_stub = h.dashboard_stub();
    grpc::ClientContext sub_ctx;
    auto sub_reader = dash_stub->Subscribe(&sub_ctx, DashboardSubscription{});

    ControlEnvelope ack_env;
    auto* ack = ack_env.mutable_ack();
    ack->set_command_id("cmd-test-1");
    ack->set_completed(true);
    ack->set_success(true);
    REQUIRE(stream->Write(ack_env));

    // Find the ack on the dashboard stream (we'll see other events too,
    // including the command we just sent — so we filter for kAck).
    bool saw_completed_ack = false;
    for (int i = 0; i < 10 && !saw_completed_ack; ++i) {
        DashboardEvent ev;
        if (!sub_reader->Read(&ev))
            break;
        if (ev.payload_case() == DashboardEvent::kAck && ev.ack().completed()) {
            REQUIRE(ev.ack().command_id() == "cmd-test-1");
            REQUIRE(ev.ack().success());
            saw_completed_ack = true;
        }
    }
    REQUIRE(saw_completed_ack);

    sub_ctx.TryCancel();
    sub_reader->Finish();

    stream->WritesDone();
    ctx.TryCancel();
    stream->Finish();
}

TEST_CASE("integration: IssueCommand publishes both command and synthesized ack",
          "[integration][dashboard][command]") {
    ServerHarness h;
    auto dash_stub = h.dashboard_stub();

    // Subscribe before issuing so we catch both events.
    grpc::ClientContext sub_ctx;
    auto sub_reader = dash_stub->Subscribe(&sub_ctx, DashboardSubscription{});

    // Tiny pause so the server registers the subscription before the
    // publish that follows (the broadcaster's replay-since-id covers
    // the worst case anyway).
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    grpc::ClientContext issue_ctx;
    GateCommand cmd;
    cmd.set_gate_id("gate-north");
    cmd.set_kind(CommandKind::COMMAND_KIND_OPEN_GATE);
    cmd.set_actor("guard:alice");
    CommandAck ack;
    REQUIRE(dash_stub->IssueCommand(&issue_ctx, cmd, &ack).ok());
    REQUIRE_FALSE(ack.command_id().empty());
    REQUIRE_FALSE(ack.completed());

    bool saw_command = false;
    bool saw_ack = false;
    for (int i = 0; i < 10 && (!saw_command || !saw_ack); ++i) {
        DashboardEvent ev;
        if (!sub_reader->Read(&ev))
            break;
        if (ev.payload_case() == DashboardEvent::kCommand &&
            ev.command().command_id() == ack.command_id()) {
            saw_command = true;
        } else if (ev.payload_case() == DashboardEvent::kAck &&
                   ev.ack().command_id() == ack.command_id()) {
            saw_ack = true;
        }
    }
    REQUIRE(saw_command);
    REQUIRE(saw_ack);

    sub_ctx.TryCancel();
    sub_reader->Finish();
}

TEST_CASE("integration: Authorize over allowlist hit shows up on dashboard subscription",
          "[integration][dashboard][authorize][subscribe]") {
    ServerHarness h;
    {
        std::vector<Allowlistentry> seed;
        Allowlistentry e;
        e.set_plate_text("KBZ123A");
        seed.push_back(e);
        h.store().upsert("site-a", seed);
    }
    auto dash_stub = h.dashboard_stub();

    grpc::ClientContext sub_ctx;
    auto sub_reader = dash_stub->Subscribe(&sub_ctx, DashboardSubscription{});
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    // Issue an Authorize call; the resulting decision should appear on
    // the subscription stream.
    AuthorizeRequest areq;
    areq.set_site_id("site-a");
    areq.set_actor("auto");
    auto* f = areq.mutable_frame();
    f->set_gate_id("gate-north");
    auto* p = f->add_plates();
    p->set_plate_text("KBZ123A");
    p->set_detection_conf(0.9f);
    p->set_ocr_conf(0.85f);
    auto* v = f->add_vehicles();
    v->set_vehicle_class(VehicleClass::VEHICLE_CLASS_SEDAN);
    v->set_class_conf(0.95f);

    grpc::ClientContext auth_ctx;
    AuthDecision resp;
    REQUIRE(dash_stub->Authorize(&auth_ctx, areq, &resp).ok());
    REQUIRE(resp.verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);

    DashboardEvent ev;
    REQUIRE(sub_reader->Read(&ev));
    REQUIRE(ev.payload_case() == DashboardEvent::kDecision);
    REQUIRE(ev.decision().decision_id() == resp.decision_id());
    REQUIRE(ev.decision().verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);

    sub_ctx.TryCancel();
    sub_reader->Finish();
}
