// dashboard_service_test.cpp — unit tests for IssueCommand + Authorize.
//
// Subscribe is server-streaming and best tested with a real gRPC channel
// (Phase 4.3.6 integration tests). Here we exercise the two unary RPCs
// against a real broadcaster + fusion engine so the wiring contract is
// covered without a network round-trip.

#include "rpc/dashboard_service.hpp"

#include <catch2/catch_test_macros.hpp>
#include <grpcpp/server_context.h>

using gate::auth::AllowlistStore;
using gate::dash::EventBroadcaster;
using gate::fusion::FusionEngine;
using gate::rpc::DashboardServiceImpl;
using gate::v1::AuthDecision;
using gate::v1::AuthorizeRequest;
using gate::v1::AuthVerdict;
using gate::v1::CommandAck;
using gate::v1::CommandKind;
using gate::v1::DashboardEvent;
using gate::v1::DashboardSubscription;
using gate::v1::GateCommand;
using gate::v1::PlateDetection;
using gate::v1::VehicleClass;
using gate::v1::VehicleDetection;

TEST_CASE("DashboardService::IssueCommand stamps id+ts and publishes both command and ack",
          "[rpc][dashboard][command]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    FusionEngine fusion{store};
    EventBroadcaster bus;
    DashboardServiceImpl svc{bus, fusion, "site-a"};

    auto sub = bus.subscribe(DashboardSubscription{});

    GateCommand req;
    req.set_gate_id("gate-north");
    req.set_kind(CommandKind::COMMAND_KIND_OPEN_GATE);
    req.set_actor("guard:alice");

    grpc::ServerContext ctx;
    CommandAck resp;
    REQUIRE(svc.IssueCommand(&ctx, &req, &resp).ok());

    REQUIRE_FALSE(resp.command_id().empty());
    REQUIRE(resp.received_ts().seconds() > 0);
    REQUIRE_FALSE(resp.completed());

    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 2);
    REQUIRE(evs[0].payload_case() == DashboardEvent::kCommand);
    REQUIRE(evs[0].command().gate_id() == "gate-north");
    REQUIRE(evs[0].command().command_id() == resp.command_id());
    REQUIRE(evs[1].payload_case() == DashboardEvent::kAck);
    REQUIRE(evs[1].ack().command_id() == resp.command_id());
}

TEST_CASE("DashboardService::IssueCommand rejects empty gate_id and unspecified kind",
          "[rpc][dashboard][command][validation]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    FusionEngine fusion{store};
    EventBroadcaster bus;
    DashboardServiceImpl svc{bus, fusion, "site-a"};
    grpc::ServerContext ctx;

    GateCommand req;
    req.set_kind(CommandKind::COMMAND_KIND_OPEN_GATE);
    CommandAck resp;
    auto s = svc.IssueCommand(&ctx, &req, &resp);
    REQUIRE_FALSE(s.ok());
    REQUIRE(s.error_code() == grpc::StatusCode::INVALID_ARGUMENT);

    req.set_gate_id("g");
    req.set_kind(CommandKind::COMMAND_KIND_UNSPECIFIED);
    s = svc.IssueCommand(&ctx, &req, &resp);
    REQUIRE_FALSE(s.ok());
    REQUIRE(s.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_CASE("DashboardService::Authorize delegates to fusion engine and publishes the decision",
          "[rpc][dashboard][authorize]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    std::vector<gate::v1::Allowlistentry> seed;
    {
        gate::v1::Allowlistentry e;
        e.set_plate_text("KBZ123A");
        seed.push_back(e);
    }
    store.upsert("site-a", seed);

    FusionEngine fusion{store};
    EventBroadcaster bus;
    DashboardServiceImpl svc{bus, fusion, "site-a"};

    auto sub = bus.subscribe(DashboardSubscription{});

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

    grpc::ServerContext ctx;
    AuthDecision resp;
    REQUIRE(svc.Authorize(&ctx, &req, &resp).ok());
    REQUIRE(resp.verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);
    REQUIRE(resp.matched_plate() == "KBZ123A");

    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 1);
    REQUIRE(evs[0].payload_case() == DashboardEvent::kDecision);
    REQUIRE(evs[0].decision().verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);
    REQUIRE(evs[0].decision().decision_id() == resp.decision_id());
}

TEST_CASE("DashboardService::IssueCommand wires LATCH commands to fusion override",
          "[rpc][dashboard][command][override]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    FusionEngine fusion{store};
    EventBroadcaster bus;
    DashboardServiceImpl svc{bus, fusion, "site-a"};
    grpc::ServerContext ctx;

    GateCommand cmd;
    cmd.set_gate_id("gate-north");
    cmd.set_kind(CommandKind::COMMAND_KIND_LATCH_OPEN);
    CommandAck ack;
    REQUIRE(svc.IssueCommand(&ctx, &cmd, &ack).ok());
    REQUIRE(fusion.override_for("gate-north") == gate::fusion::OverrideState::kForceOpen);

    cmd.set_kind(CommandKind::COMMAND_KIND_LATCH_CLOSE);
    REQUIRE(svc.IssueCommand(&ctx, &cmd, &ack).ok());
    REQUIRE(fusion.override_for("gate-north") == gate::fusion::OverrideState::kForceClose);

    cmd.set_kind(CommandKind::COMMAND_KIND_RELEASE_LATCH);
    REQUIRE(svc.IssueCommand(&ctx, &cmd, &ack).ok());
    REQUIRE(fusion.override_for("gate-north") == gate::fusion::OverrideState::kNone);

    // Non-latch commands leave override state alone.
    fusion.set_override("gate-north", gate::fusion::OverrideState::kForceOpen);
    cmd.set_kind(CommandKind::COMMAND_KIND_OPEN_GATE);
    REQUIRE(svc.IssueCommand(&ctx, &cmd, &ack).ok());
    REQUIRE(fusion.override_for("gate-north") == gate::fusion::OverrideState::kForceOpen);
}

TEST_CASE("DashboardService::Authorize rejects request missing site_id or gate_id",
          "[rpc][dashboard][authorize][validation]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    FusionEngine fusion{store};
    EventBroadcaster bus;
    DashboardServiceImpl svc{bus, fusion, "site-a"};
    grpc::ServerContext ctx;

    AuthorizeRequest req;
    AuthDecision resp;
    auto s = svc.Authorize(&ctx, &req, &resp);
    REQUIRE_FALSE(s.ok());
    REQUIRE(s.error_code() == grpc::StatusCode::INVALID_ARGUMENT);

    req.set_site_id("site-a");
    s = svc.Authorize(&ctx, &req, &resp);
    REQUIRE_FALSE(s.ok());
    REQUIRE(s.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}
