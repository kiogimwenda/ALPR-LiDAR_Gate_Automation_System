// dashboard_service.cpp

#include "rpc/dashboard_service.hpp"

#include <ctime>
#include <utility>

namespace gate::rpc {

using gate::v1::AuthDecision;
using gate::v1::AuthorizeRequest;
using gate::v1::CommandAck;
using gate::v1::DashboardEvent;
using gate::v1::DashboardSubscription;
using gate::v1::GateCommand;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using grpc::StatusCode;

DashboardServiceImpl::DashboardServiceImpl(gate::dash::EventBroadcaster& bus,
                                           gate::fusion::FusionEngine& fusion,
                                           std::string default_site_id)
    : bus_(bus), fusion_(fusion), default_site_id_(std::move(default_site_id)) {}

void DashboardServiceImpl::set_subscribe_poll_interval(std::chrono::milliseconds dt) {
    subscribe_poll_ = dt;
}

Status DashboardServiceImpl::Subscribe(ServerContext* ctx, const DashboardSubscription* req,
                                       ServerWriter<DashboardEvent>* writer) {
    auto sub = bus_.subscribe(*req);
    if (!sub) {
        return {StatusCode::UNAVAILABLE, "broadcaster is shutting down"};
    }

    // Drain any replayed events first — they're already queued from the
    // ring buffer at subscribe-time and don't need a wait.
    for (const auto& e : sub->drain_now()) {
        if (ctx->IsCancelled())
            return Status::OK;
        if (!writer->Write(e))
            return Status::OK;
    }

    // Live loop. Each next() poll has a bounded wait so we re-check
    // cancellation periodically — gRPC has no portable "wait on writer
    // cancel" primitive that composes with our condition variable.
    while (!ctx->IsCancelled()) {
        auto e = sub->next(subscribe_poll_);
        if (!e) {
            // Timeout, broadcaster stopped, or sub closed. Re-loop and
            // let the cancellation check decide whether to exit.
            if (bus_.stopped())
                return Status::OK;
            continue;
        }
        if (!writer->Write(*e))
            return Status::OK;  // Client gone.
    }
    return Status::OK;
}

Status DashboardServiceImpl::IssueCommand(ServerContext* /*ctx*/, const GateCommand* req,
                                          CommandAck* resp) {
    if (req->gate_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "gate_id is required"};
    }
    if (req->kind() == gate::v1::CommandKind::COMMAND_KIND_UNSPECIFIED) {
        return {StatusCode::INVALID_ARGUMENT, "command kind must be set"};
    }

    // Stamp issued_ts and a fresh command_id if the dashboard didn't
    // already supply one (most clients won't).
    GateCommand command{*req};
    if (command.command_id().empty()) {
        command.set_command_id(gate::fusion::generate_uuidv4());
    }
    if (!command.has_issued_ts() || command.issued_ts().seconds() == 0) {
        command.mutable_issued_ts()->set_seconds(std::time(nullptr));
    }
    const std::string command_id = command.command_id();
    const std::string gate_id = command.gate_id();

    // Publish the command itself (fan-out so other dashboards see it).
    bus_.publish_command(default_site_id_, std::move(command));

    // Synthesize a "received" ack — completion comes from firmware later
    // via the FieldControllerService stream and arrives on the dashboard
    // via the same event bus.
    resp->set_command_id(command_id);
    resp->mutable_received_ts()->set_seconds(std::time(nullptr));
    resp->set_completed(false);
    resp->set_success(false);

    // Mirror the synthesized ack on the bus so a dashboard subscribed
    // before issuing also gets the receipt confirmation.
    CommandAck ack_copy{*resp};
    bus_.publish_ack(default_site_id_, gate_id, std::move(ack_copy));

    return Status::OK;
}

Status DashboardServiceImpl::Authorize(ServerContext* /*ctx*/, const AuthorizeRequest* req,
                                       AuthDecision* resp) {
    if (req->site_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "site_id is required"};
    }
    if (req->frame().gate_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "frame.gate_id is required"};
    }
    *resp = fusion_.decide(*req);
    AuthDecision copy{*resp};
    bus_.publish_decision(req->site_id(), std::move(copy));
    return Status::OK;
}

}  // namespace gate::rpc
