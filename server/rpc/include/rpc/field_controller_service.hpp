// field_controller_service.hpp — gRPC service consumed by ESP32 field PCBs.
//
// Four RPCs from the proto:
//
//   - Control            : long-lived bidi stream. Firmware opens it on boot
//                          and sends Telemetry/CommandAck/FaultEvent over the
//                          uplink; the server pushes GateCommand back over
//                          the downlink. Both sides are multiplexed via
//                          ControlEnvelope (oneof payload).
//   - DeliverOta         : server-streaming OTA chunk delivery. Stubbed here
//                          and implemented in Phase 4.5 alongside the
//                          firmware OTA module.
//   - ReportOtaProgress  : client-streaming OTA progress beats. Stubbed.
//   - SubmitDetection    : unary — firmware-side ALPR shortcut. The dev-mode
//                          path that lets a firmware (or simulator) push a
//                          fully-formed DetectionFrame and get an
//                          AuthDecision back. Production capture lives on
//                          the server (Phase 4.9).

#pragma once

#include <chrono>
#include <google/protobuf/empty.pb.h>
#include <grpcpp/support/status.h>
#include <string>

#include "dash/event_broadcaster.hpp"
#include "fusion/fusion_engine.hpp"
#include "gate_service.grpc.pb.h"

namespace gate::rpc {

class FieldControllerServiceImpl final : public gate::v1::FieldControllerService::Service {
public:
    FieldControllerServiceImpl(gate::dash::EventBroadcaster& bus,
                               gate::fusion::FusionEngine& fusion, std::string default_site_id);

    grpc::Status Control(grpc::ServerContext* ctx,
                         grpc::ServerReaderWriter<gate::v1::ControlEnvelope,
                                                  gate::v1::ControlEnvelope>* stream) override;

    grpc::Status DeliverOta(grpc::ServerContext* ctx, const gate::v1::OtaManifest* req,
                            grpc::ServerWriter<gate::v1::OtaChunk>* writer) override;

    grpc::Status ReportOtaProgress(grpc::ServerContext* ctx,
                                   grpc::ServerReader<gate::v1::OtaProgress>* reader,
                                   google::protobuf::Empty* resp) override;

    grpc::Status SubmitDetection(grpc::ServerContext* ctx, const gate::v1::DetectionFrame* req,
                                 gate::v1::AuthDecision* resp) override;

    // How long the writer thread blocks per next() call before re-checking
    // whether the gRPC context has been cancelled. Tests shorten this.
    void set_subscribe_poll_interval(std::chrono::milliseconds dt);

private:
    gate::dash::EventBroadcaster& bus_;
    gate::fusion::FusionEngine& fusion_;
    std::string default_site_id_;
    std::chrono::milliseconds poll_{500};
};

}  // namespace gate::rpc
