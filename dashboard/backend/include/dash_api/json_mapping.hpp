// json_mapping.hpp — gate.v1 protobuf → JSON for the dashboard API.
//
// One mapping, two consumers: REST snapshot/response bodies (4.7.2)
// and WebSocket event frames (4.7.3). Field names are camelCase (the
// frontend's native convention); enums map to their proto short names
// minus the prefix ("AUTHORIZED", "GATE_STATE_OPEN" → "OPEN") so the
// UI switches on stable strings instead of magic integers; timestamps
// serialise as both epoch milliseconds ("tsMs", for sorting/charts)
// and ISO-8601 UTC ("ts", for humans reading raw payloads).
//
// Depends only on gate_proto + jsoncpp — no Drogon — so the whole
// layer is unit-testable in tests/dashboard_api/.

#pragma once

#include <json/json.h>

#include "gate_service.pb.h"

namespace gate::dash_api {

Json::Value to_json(const google::protobuf::Timestamp& ts);  // {"ts": ..., "tsMs": ...}
Json::Value to_json(const gate::v1::Telemetry& t);
Json::Value to_json(const gate::v1::AuthDecision& d);
Json::Value to_json(const gate::v1::FaultEvent& f);
Json::Value to_json(const gate::v1::OtaProgress& p);
Json::Value to_json(const gate::v1::GateCommand& c);
Json::Value to_json(const gate::v1::CommandAck& a);

// Dispatches on the payload oneof; adds eventId/type/ts envelope
// fields. `type` is one of: decision, telemetry, fault, ota, command,
// ack, unknown.
Json::Value to_json(const gate::v1::DashboardEvent& ev);

// Enum short names ("OPEN", "AUTHORIZED", "DENY_REASON_NOT_FOUND"
// stripped to "NOT_FOUND", …). Unknown values render as the integer.
std::string gate_state_name(gate::v1::GateState s);
std::string verdict_name(gate::v1::AuthVerdict v);

// --- Allowlist (REST CRUD, 4.7.2) -------------------------------------------

Json::Value to_json(const gate::v1::Allowlistentry& e);
Json::Value to_json(const gate::v1::ListAllowlistResponse& r);
Json::Value to_json(const gate::v1::UpsertAllowlistResponse& r);

// Inbound parsing. Returns false and fills `error` on a body the API
// must reject (missing plate, unknown class name, malformed window).
// Accepted shape mirrors to_json(Allowlistentry):
//   { "plate": "KDA123X", "ownerName": …, "ownerUnit": …, "notes": …,
//     "allowedClasses": ["SEDAN", …],
//     "timeWindows": [{"startMinute":360,"endMinute":1080,"daysMask":31}],
//     "validFromMs": …, "validUntilMs": … }
bool allowlist_entry_from_json(const Json::Value& v, gate::v1::Allowlistentry& out,
                               std::string& error);

// "OPEN_GATE" → COMMAND_KIND_OPEN_GATE; false on unknown/UNSPECIFIED.
bool command_kind_from_string(const std::string& s, gate::v1::CommandKind& out);

}  // namespace gate::dash_api
