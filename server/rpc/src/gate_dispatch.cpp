// gate_dispatch.cpp — see the header.

#include "rpc/gate_dispatch.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <utility>

namespace gate::rpc {

using gate::v1::OpenReply;

namespace {

// 16 binary bytes back to the UUID text the arbiter keys on.
std::string uuid_text(const std::string& b) {
    if (b.size() != 16) {
        return b;
    }
    char out[37];
    const auto* p = reinterpret_cast<const unsigned char*>(b.data());
    std::snprintf(out, sizeof(out),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", p[0],
                  p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13],
                  p[14], p[15]);
    return out;
}

gate::v1::FaultSeverity alert_severity(const std::string& kind) {
    return kind == "BLOCKLIST_FORCED" ? gate::v1::FAULT_SEVERITY_CRITICAL
                                      : gate::v1::FAULT_SEVERITY_WARN;
}

}  // namespace

GateDispatch::GateDispatch(gate::dash::EventBroadcaster& bus, gate::auth::AllowlistStore* store,
                           gate::fusion::FusionEngine* fusion, std::string site_id)
    : bus_(bus), store_(store), fusion_(fusion), site_id_(std::move(site_id)) {}

std::uint64_t GateDispatch::now_ms() const {
    if (fixed_now_ms_ != 0) {
        return fixed_now_ms_;
    }
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

GateDispatch::Gate& GateDispatch::gate_locked(const std::string& gate_id) {
    auto& g = gates_[gate_id];
    if (!g.arbiter) {
        g.arbiter = std::make_unique<GateArbiter>(
            GateArbiter::Config{},
            [this, gate_id](const GateArbiter::Request& r) { publish_open(gate_id, r); },
            [this, gate_id](const GateArbiter::Audit& a) { audit(gate_id, a); },
            [this, gate_id](const GateArbiter::Request& r) {
                // Rule 8: lockdown or a new block since the car queued.
                if (fusion_ != nullptr &&
                    fusion_->override_for(gate_id) == gate::fusion::OverrideState::kForceClose) {
                    return false;
                }
                return store_ == nullptr || r.vehicle_key.empty() ||
                       !store_->is_blocklisted(site_id_, r.vehicle_key);
            });
    }
    return g;
}

void GateDispatch::publish_open(const std::string& gate_id, const GateArbiter::Request& r) {
    gate::v1::GateCommand c;
    c.set_command_id(r.command_id.empty() ? gate::fusion::generate_uuidv4() : r.command_id);
    const auto now = std::time(nullptr);
    c.mutable_issued_ts()->set_seconds(now);
    c.set_gate_id(gate_id);
    c.set_kind(gate::v1::COMMAND_KIND_OPEN_GATE);
    c.set_actor(r.actor);
    c.set_source(r.source);
    c.set_side(r.side);
    c.set_request_id(r.request_id);
    // Legacy firmware has no expiry check: spare it the field.
    if (gates_[gate_id].capabilities != 0) {
        c.mutable_expires_ts()->set_seconds(now + kOpenExpirySeconds);
    }
    bus_.publish_command(site_id_, std::move(c));
}

void GateDispatch::audit(const std::string& gate_id, const GateArbiter::Audit& a) {
    gate::v1::OpenRequestRecord rec;
    auto* q = rec.mutable_request();
    q->set_request_id(a.request.request_id);
    q->set_gate_id(gate_id);
    q->set_side(a.request.side);
    q->set_source(a.request.source);
    rec.set_actor(a.request.actor);
    rec.set_decision_id(a.request.decision_id);
    rec.set_queued_ms(a.queued_ms);
    rec.set_preempted_by(a.preempted_by);
    rec.set_outcome(a.outcome);
    // Never opened: give the guest pass back. "abandoned" is not here:
    // the command went out, and its ack settles the claim.
    if (store_ != nullptr && !a.request.pass_id.empty() && a.outcome != "granted" &&
        a.outcome != "abandoned") {
        store_->guest_pass_settle(a.request.command_id, false);
    }
    if (store_ != nullptr) {
        store_->open_audit_insert(rec, std::time(nullptr));
    }
    bus_.publish_open_request(site_id_, std::move(rec));
}

OpenReply GateDispatch::submit(const std::string& gate_id, GateArbiter::Request r) {
    const std::lock_guard<std::mutex> lk{mu_};
    if (r.request_id.empty()) {
        r.request_id = gate::fusion::generate_uuidv4();
    }
    return gate_locked(gate_id).arbiter->submit(r, now_ms());
}

OpenReply GateDispatch::submit_decision(const gate::v1::AuthDecision& d,
                                        gate::v1::OpenSource source) {
    GateArbiter::Request r;
    r.request_id = d.decision_id();
    r.side = d.side() == gate::v1::GATE_SIDE_UNSPECIFIED ? gate::v1::GATE_SIDE_ENTRY : d.side();
    r.source = source;
    r.vehicle_key = d.force_reason().empty() ? d.matched_plate() : "";
    r.actor = "auto:" + d.decision_id();
    if (!d.force_reason().empty()) {
        r.actor = d.actor();
    }
    r.decision_id = d.decision_id();
    // A guest's exit reuses a spent pass and takes no claim.
    if (!d.pass_id().empty() && r.side != gate::v1::GATE_SIDE_EXIT) {
        r.pass_id = d.pass_id();
        r.command_id = gate::fusion::generate_uuidv4();
        if (store_ != nullptr &&
            !store_->guest_pass_reserve(r.pass_id, r.command_id, std::time(nullptr))) {
            OpenReply refused;  // lost the race for a single visit
            refused.set_request_id(r.request_id);
            refused.set_status(OpenReply::DENIED);
            const std::lock_guard<std::mutex> lk{mu_};
            audit(d.gate_id(), {r, 0, "", "pass_unavailable"});
            return refused;
        }
    }
    return submit(d.gate_id(), std::move(r));
}

OpenReply GateDispatch::on_open_request(const std::string& gate_id,
                                        const gate::v1::OpenRequest& q) {
    if (q.offline_served()) {
        const std::lock_guard<std::mutex> lk{mu_};
        gate_locked(gate_id).arbiter->mark_served_offline(q.request_id(), now_ms());
        OpenReply reply;
        reply.set_request_id(q.request_id());
        reply.set_status(OpenReply::ALREADY_SERVED);
        return reply;
    }
    GateArbiter::Request r;
    r.request_id = q.request_id();
    r.side = q.side() == gate::v1::GATE_SIDE_UNSPECIFIED ? gate::v1::GATE_SIDE_EXIT : q.side();
    r.source = q.source();
    r.actor = "controller";
    return submit(gate_id, std::move(r));
}

void GateDispatch::on_telemetry(const gate::v1::Telemetry& t) {
    const std::lock_guard<std::mutex> lk{mu_};
    auto& g = gate_locked(t.gate_id());
    const std::uint64_t now = now_ms();
    g.capabilities = t.capabilities();
    const bool cleared = g.beam_tripped && t.safety_beam_clear();
    const bool shut =
        t.gate_state() == gate::v1::GATE_STATE_CLOSED && g.state != gate::v1::GATE_STATE_CLOSED;
    g.beam_tripped = !t.safety_beam_clear();
    g.state = t.gate_state();
    if (cleared || shut) {
        g.arbiter->vehicle_cleared(now);
    } else {
        g.arbiter->tick(now);
    }
}

void GateDispatch::on_ack(const gate::v1::CommandAck& a) {
    if (!a.completed()) {
        return;
    }
    // A failed open must not burn the pass. Unknown ids are no-ops.
    if (store_ != nullptr) {
        store_->guest_pass_settle(a.command_id(), a.success());
    }
}

gate::v1::EventAck GateDispatch::ingest(const std::string& gate_id,
                                        const gate::v1::EventBatch& batch) {
    gate::v1::EventAck ack;
    const std::lock_guard<std::mutex> lk{mu_};
    auto& g = gate_locked(gate_id);
    for (const auto& e : batch.events()) {
        const bool fresh = store_ == nullptr || store_->controller_event_insert(gate_id, e);
        if (fresh && e.kind() == kEventOfflineOpen) {
            g.arbiter->mark_served_offline(uuid_text(e.request_id()), now_ms());
        }
        ack.set_boot_id(e.boot_id());
        ack.set_last_seq(std::max(ack.last_seq(), e.seq()));
    }
    return ack;
}

void GateDispatch::publish_alerts(const gate::v1::AuthDecision& d) {
    for (const auto& kind : d.alerts()) {
        gate::v1::Alert a;
        a.set_ref(d.decision_id());
        a.set_kind(kind);
        a.set_severity(alert_severity(kind));
        a.set_gate_id(d.gate_id());
        a.set_plate(d.matched_plate());
        a.set_message(d.force_reason().empty() ? d.reason_text()
                                               : d.actor() + ": " + d.force_reason());
        bus_.publish_alert(site_id_, std::move(a));
    }
}

bool GateDispatch::supports(const std::string& gate_id, gate::v1::CommandKind kind) {
    std::uint32_t need = 0;
    switch (kind) {
        case gate::v1::COMMAND_KIND_CONFIGURE:
        case gate::v1::COMMAND_KIND_LOCKDOWN_ON:
        case gate::v1::COMMAND_KIND_LOCKDOWN_OFF:
        case gate::v1::COMMAND_KIND_CLEAR_FAULT:
            need = kCapLocks;
            break;
        case gate::v1::COMMAND_KIND_VERIFY_TYPE:
        case gate::v1::COMMAND_KIND_CALIBRATE:
            need = kCapVerify;
            break;
        case gate::v1::COMMAND_KIND_REQUEST_STATS:
            need = kCapStats;
            break;
        default:
            return true;
    }
    const std::lock_guard<std::mutex> lk{mu_};
    return (gate_locked(gate_id).capabilities & need) != 0;
}

bool GateDispatch::locked(const std::string& gate_id) {
    const std::lock_guard<std::mutex> lk{mu_};
    return (gate_locked(gate_id).capabilities & kCapLocks) != 0;
}

bool GateDispatch::hold_auto_close(const std::string& gate_id) {
    const std::lock_guard<std::mutex> lk{mu_};
    return gate_locked(gate_id).arbiter->hold_auto_close();
}

void GateDispatch::vehicle_left(const std::string& gate_id, const std::string& plate) {
    const std::lock_guard<std::mutex> lk{mu_};
    gate_locked(gate_id).arbiter->withdraw(plate, now_ms());
}

}  // namespace gate::rpc
