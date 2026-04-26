// field_controller_service_test.cpp — handler-level tests.
//
// SubmitDetection + DeliverOta + ReportOtaProgress can be tested via direct
// handler calls. The bidi Control stream is exercised via a real gRPC channel
// in tests/integration/4.3.6.

#include "rpc/field_controller_service.hpp"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/empty.pb.h>
#include <grpcpp/server_context.h>

using gate::auth::AllowlistStore;
using gate::dash::EventBroadcaster;
using gate::fusion::FusionEngine;
using gate::rpc::FieldControllerServiceImpl;
using gate::v1::Allowlistentry;
using gate::v1::AuthDecision;
using gate::v1::AuthVerdict;
using gate::v1::DashboardEvent;
using gate::v1::DashboardSubscription;
using gate::v1::DetectionFrame;
using gate::v1::OtaManifest;
using gate::v1::PlateDetection;
using gate::v1::VehicleClass;
using gate::v1::VehicleDetection;

TEST_CASE("FieldControllerService::SubmitDetection delegates to fusion + publishes decision",
          "[rpc][field][submit]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    std::vector<Allowlistentry> seed;
    {
        Allowlistentry e;
        e.set_plate_text("KBZ123A");
        seed.push_back(e);
    }
    store.upsert("default", seed);

    FusionEngine fusion{store};
    EventBroadcaster bus;
    FieldControllerServiceImpl svc{bus, fusion, "default"};

    auto sub = bus.subscribe(DashboardSubscription{});

    DetectionFrame frame;
    frame.set_gate_id("gate-north");
    frame.set_frame_id(7);
    auto* p = frame.add_plates();
    p->set_plate_text("KBZ-123A");
    p->set_detection_conf(0.9f);
    p->set_ocr_conf(0.85f);
    auto* v = frame.add_vehicles();
    v->set_class_(VehicleClass::VEHICLE_CLASS_SEDAN);
    v->set_class_conf(0.95f);

    grpc::ServerContext ctx;
    AuthDecision resp;
    REQUIRE(svc.SubmitDetection(&ctx, &frame, &resp).ok());
    REQUIRE(resp.verdict() == AuthVerdict::AUTH_VERDICT_AUTHORIZED);
    REQUIRE(resp.actor() == "firmware-submit");
    REQUIRE(resp.frame_id() == 7);

    const auto evs = sub->drain_now();
    REQUIRE(evs.size() == 1);
    REQUIRE(evs[0].payload_case() == DashboardEvent::kDecision);
    REQUIRE(evs[0].decision().decision_id() == resp.decision_id());
}

TEST_CASE("FieldControllerService::SubmitDetection rejects empty gate_id",
          "[rpc][field][submit][validation]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    FusionEngine fusion{store};
    EventBroadcaster bus;
    FieldControllerServiceImpl svc{bus, fusion, "default"};
    grpc::ServerContext ctx;

    DetectionFrame frame;  // gate_id empty
    AuthDecision resp;
    const auto status = svc.SubmitDetection(&ctx, &frame, &resp);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_CASE("FieldControllerService::DeliverOta returns UNIMPLEMENTED until Phase 4.5",
          "[rpc][field][ota]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    FusionEngine fusion{store};
    EventBroadcaster bus;
    FieldControllerServiceImpl svc{bus, fusion, "default"};
    grpc::ServerContext ctx;

    OtaManifest req;
    // The writer parameter is null because the handler refuses before
    // touching it — a real client never reaches the stream.
    const auto status = svc.DeliverOta(&ctx, &req, nullptr);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.error_code() == grpc::StatusCode::UNIMPLEMENTED);
}

TEST_CASE("FieldControllerService::ReportOtaProgress returns UNIMPLEMENTED until Phase 4.5",
          "[rpc][field][ota]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    FusionEngine fusion{store};
    EventBroadcaster bus;
    FieldControllerServiceImpl svc{bus, fusion, "default"};
    grpc::ServerContext ctx;
    google::protobuf::Empty resp;
    const auto status = svc.ReportOtaProgress(&ctx, nullptr, &resp);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.error_code() == grpc::StatusCode::UNIMPLEMENTED);
}
