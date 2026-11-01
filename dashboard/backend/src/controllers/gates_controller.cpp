// gates_controller.cpp — POST /api/gates/{gateId}/command.
//
// Body: {"kind": "OPEN_GATE" | "CLOSE_GATE" | "LATCH_OPEN" |
//        "LATCH_CLOSE" | "RELEASE_LATCH" | "REBOOT" | …,
//        "ledPattern": 0-5 (only for LED_PATTERN)}
//
// The server stamps command_id/issued_ts and answers with the initial
// CommandAck (the completion ack arrives later on the event stream —
// 4.7.3); the response body is that ack under the presentation
// mapping, so the frontend correlates by commandId.

#include <drogon/HttpController.h>

#include "dash_api/grpc_bridge.hpp"
#include "dash_api/json_mapping.hpp"
#include "grpc_http.hpp"
#include "request_identity.hpp"

namespace gate::dash_api {

class GatesController : public drogon::HttpController<GatesController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(GatesController::command, "/api/gates/{gate_id}/command", drogon::Post,
                  "gate::dash_api::OperatorFilter");
    ADD_METHOD_TO(GatesController::configure, "/api/gates/{gate_id}/config", drogon::Put,
                  "gate::dash_api::AdminFilter");
    METHOD_LIST_END

    // Body: {"autoCloseS", "offlineCloseS", "boltHoldOpenS"}. Refused
    // with 409-class errors unless the gate is closed.
    void configure(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   std::string gate_id) const {
        const auto b = bridge_or_error(callback);
        if (b == nullptr) {
            return;
        }
        const auto body = req->getJsonObject();
        if (body == nullptr) {
            callback(json_error(drogon::k400BadRequest, "body must be JSON"));
            return;
        }
        gate::v1::SetGateConfigRequest in;
        in.set_gate_id(gate_id);
        in.set_updated_by(request_user(req));
        // Merge over what is stored: an absent field keeps its value.
        // A failed read just means this is the first config.
        gate::v1::GateConfigRecord stored;
        if (b->get_gate_config(gate_id, stored).ok()) {
            *in.mutable_config() = stored.config();
        }
        auto* c = in.mutable_config();
        if (body->isMember("autoCloseS")) {
            c->set_auto_close_s((*body)["autoCloseS"].asUInt());
        }
        if (body->isMember("offlineCloseS")) {
            c->set_offline_close_s((*body)["offlineCloseS"].asUInt());
        }
        if (body->isMember("boltHoldOpenS")) {
            c->set_bolt_hold_open_s((*body)["boltHoldOpenS"].asUInt());
        }
        gate::v1::GateConfigRecord rec;
        const auto result = b->set_gate_config(std::move(in), rec);
        if (!result.ok()) {
            callback(json_error(http_status(result.code), result.message));
            return;
        }
        Json::Value out;
        out["version"] = rec.config().version();
        callback(drogon::HttpResponse::newHttpJsonResponse(out));
    }

    void command(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 std::string gate_id) const {
        const auto b = bridge_or_error(callback);
        if (b == nullptr) {
            return;
        }
        const auto body = req->getJsonObject();
        if (body == nullptr || !(*body)["kind"].isString()) {
            callback(json_error(drogon::k400BadRequest, "body requires a string 'kind'"));
            return;
        }
        gate::v1::CommandKind kind{};
        if (!command_kind_from_string((*body)["kind"].asString(), kind)) {
            callback(json_error(drogon::k400BadRequest,
                                "unknown command kind: " + (*body)["kind"].asString()));
            return;
        }

        // A sticky force-open authorises every plate: admins only.
        if (kind == gate::v1::COMMAND_KIND_LATCH_OPEN && request_role(req) != "admin") {
            callback(json_error(drogon::k403Forbidden, "LATCH_OPEN requires the admin role"));
            return;
        }

        gate::v1::GateCommand cmd;
        cmd.set_gate_id(gate_id);
        cmd.set_kind(kind);
        cmd.set_actor(request_user(req));  // audit: who pressed the button
        if (kind == gate::v1::COMMAND_KIND_LED_PATTERN) {
            cmd.set_led_pattern(static_cast<gate::v1::LedPattern>((*body)["ledPattern"].asInt()));
        }

        gate::v1::CommandAck ack;
        const auto result = b->issue_command(std::move(cmd), ack);
        if (!result.ok()) {
            callback(json_error(http_status(result.code), result.message));
            return;
        }
        Json::Value out;
        out["ack"] = to_json(ack);
        callback(drogon::HttpResponse::newHttpJsonResponse(out));
    }
};

}  // namespace gate::dash_api
