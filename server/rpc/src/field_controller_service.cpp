// field_controller_service.cpp

#include "rpc/field_controller_service.hpp"

#include <atomic>
#include <ctime>
#include <thread>
#include <utility>

namespace gate::rpc {

using gate::v1::AuthDecision;
using gate::v1::AuthorizeRequest;
using gate::v1::ControlEnvelope;
using gate::v1::DashboardEvent;
using gate::v1::DashboardSubscription;
using gate::v1::DetectionFrame;
using gate::v1::OtaChunk;
using gate::v1::OtaManifest;
using gate::v1::OtaProgress;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::StatusCode;

FieldControllerServiceImpl::FieldControllerServiceImpl(gate::dash::EventBroadcaster& bus,
                                                       gate::fusion::FusionEngine& fusion,
                                                       std::string default_site_id)
    : bus_(bus), fusion_(fusion), default_site_id_(std::move(default_site_id)) {}

void FieldControllerServiceImpl::set_subscribe_poll_interval(std::chrono::milliseconds dt) {
    poll_ = dt;
}

Status FieldControllerServiceImpl::Control(
    ServerContext* ctx, ServerReaderWriter<ControlEnvelope, ControlEnvelope>* stream) {
    // The firmware must open the stream with a Telemetry envelope so we
    // can identify the gate. Without that we don't know how to route
    // commands back, and we'd have to guess the gate_id from later
    // payloads — too fragile.
    ControlEnvelope first;
    if (!stream->Read(&first)) {
        return {StatusCode::INVALID_ARGUMENT, "control stream closed before identifying gate"};
    }
    if (first.payload_case() != ControlEnvelope::kTelemetry) {
        return {StatusCode::INVALID_ARGUMENT, "first envelope must be Telemetry carrying gate_id"};
    }
    const std::string gate_id = first.telemetry().gate_id();
    if (gate_id.empty()) {
        return {StatusCode::INVALID_ARGUMENT, "telemetry.gate_id is required"};
    }
    bus_.publish_telemetry(default_site_id_, first.telemetry());

    // Server → firmware: subscribe to broadcaster for commands targeted
    // at this gate. The DashboardSubscription kind filter has no flag
    // for command-only, so we set include_decisions=true to engage the
    // kind filter — Commands always pass through that filter, and
    // everything else (decisions, telemetry, faults, ota) gets dropped
    // before the writer-thread check below.
    DashboardSubscription filter;
    filter.add_gate_ids(gate_id);
    filter.set_include_decisions(true);
    auto sub = bus_.subscribe(filter);
    if (!sub) {
        return {StatusCode::UNAVAILABLE, "broadcaster is shutting down"};
    }

    // Writer thread: pull commands out of the subscription queue, write
    // them to the stream. One thread per active firmware connection;
    // we expect a small number of gates per server.
    std::atomic<bool> shutting_down{false};
    std::thread writer([&] {
        while (!ctx->IsCancelled() && !shutting_down.load()) {
            auto e = sub->next(poll_);
            if (!e) {
                if (bus_.stopped())
                    break;
                continue;
            }
            // The subscription will pump anything routed to this gate;
            // we forward only Commands. (Decisions, faults, telemetry,
            // and OTA events are server-side bookkeeping and don't go
            // back to firmware.)
            if (e->payload_case() != DashboardEvent::kCommand)
                continue;
            ControlEnvelope env;
            *env.mutable_command() = e->command();
            if (!stream->Write(env))
                break;  // Client gone.
        }
    });

    // Reader loop: dispatch each incoming envelope to the bus.
    ControlEnvelope env;
    while (stream->Read(&env)) {
        switch (env.payload_case()) {
            case ControlEnvelope::kTelemetry:
                bus_.publish_telemetry(default_site_id_, env.telemetry());
                break;
            case ControlEnvelope::kAck:
                bus_.publish_ack(default_site_id_, gate_id, env.ack());
                break;
            case ControlEnvelope::kFault:
                bus_.publish_fault(default_site_id_, env.fault());
                break;
            case ControlEnvelope::kCommand:
                // Server-only payload; firmware shouldn't send this. Ignore.
                break;
            case ControlEnvelope::PAYLOAD_NOT_SET:
                break;
        }
    }

    shutting_down.store(true);
    sub->close();
    if (writer.joinable())
        writer.join();
    return Status::OK;
}

Status FieldControllerServiceImpl::DeliverOta(ServerContext* /*ctx*/, const OtaManifest* /*req*/,
                                              ServerWriter<OtaChunk>* /*writer*/) {
    // OTA delivery requires the firmware-side OTA module (Phase 4.5) and
    // signing infrastructure (Phase 4.8 deployment). Refuse cleanly so a
    // confused client gets a precise error rather than a hang.
    return {StatusCode::UNIMPLEMENTED, "OTA delivery is implemented in Phase 4.5"};
}

Status FieldControllerServiceImpl::ReportOtaProgress(ServerContext* /*ctx*/,
                                                     ServerReader<OtaProgress>* /*reader*/,
                                                     google::protobuf::Empty* /*resp*/) {
    return {StatusCode::UNIMPLEMENTED, "OTA progress reporting is implemented in Phase 4.5"};
}

Status FieldControllerServiceImpl::SubmitDetection(ServerContext* /*ctx*/,
                                                   const DetectionFrame* req, AuthDecision* resp) {
    if (req->gate_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "frame.gate_id is required"};
    }
    AuthorizeRequest areq;
    *areq.mutable_frame() = *req;
    areq.set_site_id(default_site_id_);
    areq.set_actor("firmware-submit");
    *resp = fusion_.decide(areq);
    AuthDecision copy{*resp};
    bus_.publish_decision(default_site_id_, std::move(copy));

    // An AUTHORIZED verdict *is* the product: dispatch OPEN_GATE to the
    // detected gate through the same bus IssueCommand publishes on, so
    // the gate's Control stream forwards it exactly like a dashboard
    // command. Every other verdict (denied, low-confidence, manual
    // review) stays a dashboard event — a gate only ever moves on an
    // explicit command.
    if (resp->verdict() == gate::v1::AUTH_VERDICT_AUTHORIZED) {
        gate::v1::GateCommand open;
        open.set_command_id(gate::fusion::generate_uuidv4());
        open.mutable_issued_ts()->set_seconds(std::time(nullptr));
        open.set_gate_id(req->gate_id());
        open.set_kind(gate::v1::COMMAND_KIND_OPEN_GATE);
        // Audit trail links the command back to the decision that
        // caused it, not just a generic "auto".
        open.set_actor("auto:" + resp->decision_id());
        bus_.publish_command(default_site_id_, std::move(open));
    }
    return Status::OK;
}

}  // namespace gate::rpc
