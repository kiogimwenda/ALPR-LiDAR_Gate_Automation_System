// json_mapping.cpp — protobuf → JSON, one field at a time, on purpose.
//
// A reflection-driven generic mapper (protobuf's util::MessageToJson)
// was considered and rejected: it emits proto field names verbatim,
// stringifies 64-bit ints inconsistently, and couples the wire schema
// to the frontend schema 1:1. The dashboard wants a *presentation*
// schema — short enum names, dual timestamp forms, camelCase — and an
// explicit mapping is the contract that keeps proto churn from
// silently rippling into the UI.

#include "dash_api/json_mapping.hpp"

#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <string>

namespace gate::dash_api {

namespace {

// "GATE_STATE_OPEN" + "GATE_STATE_" → "OPEN"; unknown enum values
// arrive as an empty protobuf name and render as the raw integer.
std::string short_name(const std::string& full, const char* prefix, int raw) {
    if (full.empty()) {
        return std::to_string(raw);
    }
    const std::string p(prefix);
    return full.rfind(p, 0) == 0 ? full.substr(p.size()) : full;
}

std::string iso8601_utc(std::int64_t seconds, std::int32_t nanos) {
    std::tm tm{};
    const auto t = static_cast<std::time_t>(seconds);
    gmtime_r(&t, &tm);
    char buf[40];
    const std::size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    std::snprintf(buf + n, sizeof(buf) - n, ".%03dZ", nanos / 1'000'000);
    return buf;
}

}  // namespace

Json::Value to_json(const google::protobuf::Timestamp& ts) {
    Json::Value v;
    v["ts"] = iso8601_utc(ts.seconds(), ts.nanos());
    v["tsMs"] = Json::Value::Int64{ts.seconds() * 1000 + ts.nanos() / 1'000'000};
    return v;
}

std::string gate_state_name(gate::v1::GateState s) {
    return short_name(gate::v1::GateState_Name(s), "GATE_STATE_", static_cast<int>(s));
}

std::string verdict_name(gate::v1::AuthVerdict v) {
    return short_name(gate::v1::AuthVerdict_Name(v), "AUTH_VERDICT_", static_cast<int>(v));
}

Json::Value to_json(const gate::v1::Telemetry& t) {
    Json::Value v;
    v["gateId"] = t.gate_id();
    if (t.has_sent_ts()) {
        v["sent"] = to_json(t.sent_ts());
    }
    v["state"] = gate_state_name(t.gate_state());
    v["limitOpen"] = t.limit_open();
    v["limitClosed"] = t.limit_closed();
    v["beamClear"] = t.safety_beam_clear();
    v["uptimeSec"] = t.uptime_sec();
    v["rssiDbm"] = t.rssi_dbm();
    v["freeHeapBytes"] = t.free_heap_bytes();
    v["minFreeHeap"] = t.min_free_heap();
    v["supplyVoltage"] = t.supply_voltage_v();
    v["coreTempC"] = t.core_temp_c();
    v["fwVersion"] = t.firmware_version();
    v["seq"] = Json::Value::UInt64{t.seq()};
    return v;
}

Json::Value to_json(const gate::v1::AuthDecision& d) {
    Json::Value v;
    v["decisionId"] = d.decision_id();
    if (d.has_decision_ts()) {
        v["decided"] = to_json(d.decision_ts());
    }
    v["gateId"] = d.gate_id();
    v["verdict"] = verdict_name(d.verdict());
    v["denyReason"] = short_name(gate::v1::DenyReason_Name(d.deny_reason()), "DENY_REASON_",
                                 static_cast<int>(d.deny_reason()));
    v["matchedPlate"] = d.matched_plate();
    v["matchedClass"] = short_name(gate::v1::VehicleClass_Name(d.matched_class()), "VEHICLE_CLASS_",
                                   static_cast<int>(d.matched_class()));
    v["confidence"] = d.combined_conf();
    v["reason"] = d.reason_text();
    v["frameId"] = Json::Value::UInt64{d.frame_id()};
    v["actor"] = d.actor();
    return v;
}

Json::Value to_json(const gate::v1::FaultEvent& f) {
    Json::Value v;
    v["faultId"] = f.fault_id();
    if (f.has_occurred_ts()) {
        v["occurred"] = to_json(f.occurred_ts());
    }
    v["gateId"] = f.gate_id();
    v["severity"] = short_name(gate::v1::FaultSeverity_Name(f.severity()), "FAULT_SEVERITY_",
                               static_cast<int>(f.severity()));
    v["code"] = f.code();
    v["description"] = f.description();
    Json::Value ctx(Json::objectValue);
    for (const auto& [key, value] : f.context()) {
        ctx[key] = value;
    }
    v["context"] = ctx;
    return v;
}

Json::Value to_json(const gate::v1::OtaProgress& p) {
    Json::Value v;
    v["commandId"] = p.command_id();
    v["bytesReceived"] = Json::Value::UInt64{p.bytes_received()};
    v["bytesTotal"] = Json::Value::UInt64{p.bytes_total()};
    v["phase"] = short_name(gate::v1::OtaProgress::OtaPhase_Name(p.phase()), "OTA_PHASE_",
                            static_cast<int>(p.phase()));
    return v;
}

Json::Value to_json(const gate::v1::GateCommand& c) {
    Json::Value v;
    v["commandId"] = c.command_id();
    if (c.has_issued_ts()) {
        v["issued"] = to_json(c.issued_ts());
    }
    v["gateId"] = c.gate_id();
    v["kind"] = short_name(gate::v1::CommandKind_Name(c.kind()), "COMMAND_KIND_",
                           static_cast<int>(c.kind()));
    v["actor"] = c.actor();
    return v;
}

Json::Value to_json(const gate::v1::CommandAck& a) {
    Json::Value v;
    v["commandId"] = a.command_id();
    if (a.has_received_ts()) {
        v["received"] = to_json(a.received_ts());
    }
    if (a.has_completed_ts()) {
        v["completedAt"] = to_json(a.completed_ts());
    }
    v["completed"] = a.completed();
    v["success"] = a.success();
    v["error"] = a.error_text();
    v["stateAfter"] = gate_state_name(a.state_after());
    return v;
}

Json::Value to_json(const gate::v1::DashboardEvent& ev) {
    Json::Value v;
    v["eventId"] = Json::Value::UInt64{ev.event_id()};
    if (ev.has_event_ts()) {
        v["event"] = to_json(ev.event_ts());
    }
    switch (ev.payload_case()) {
        case gate::v1::DashboardEvent::kDecision:
            v["type"] = "decision";
            v["decision"] = to_json(ev.decision());
            break;
        case gate::v1::DashboardEvent::kTelemetry:
            v["type"] = "telemetry";
            v["telemetry"] = to_json(ev.telemetry());
            break;
        case gate::v1::DashboardEvent::kFault:
            v["type"] = "fault";
            v["fault"] = to_json(ev.fault());
            break;
        case gate::v1::DashboardEvent::kOta:
            v["type"] = "ota";
            v["ota"] = to_json(ev.ota());
            break;
        case gate::v1::DashboardEvent::kCommand:
            v["type"] = "command";
            v["command"] = to_json(ev.command());
            break;
        case gate::v1::DashboardEvent::kAck:
            v["type"] = "ack";
            v["ack"] = to_json(ev.ack());
            break;
        default:
            v["type"] = "unknown";
            break;
    }
    return v;
}

}  // namespace gate::dash_api
