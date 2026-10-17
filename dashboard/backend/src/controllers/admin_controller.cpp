// admin_controller.cpp — alerts, gate configuration, users, settings.
//
//   GET    /api/alerts?unread=1&limit=&offset=   any user
//   POST   /api/alerts/{id}/ack                  operator+
//   POST   /api/alerts/ack-all                   operator+
//   GET    /api/gates                            any user — known gates + config
//   PUT    /api/gates/{gate_id}                  admin — {"name","location","direction"}
//   GET    /api/users                            admin
//   POST   /api/users                            admin — {"username","password","role",
//                                                "fullName","email","active"}
//   PUT    /api/users/{id}                       admin — same fields, password optional
//   DELETE /api/users/{id}                       admin
//   GET    /api/settings                         any user
//   PUT    /api/settings                         admin — {"retentionDays","sessionTimeoutMinutes"}

#include <drogon/HttpController.h>

#include "dash_api/auth.hpp"
#include "dash_api/history_store.hpp"
#include "grpc_http.hpp"
#include "request_identity.hpp"

namespace gate::dash_api {

namespace {

using Callback = std::function<void(const drogon::HttpResponsePtr&)>;

std::shared_ptr<HistoryStore> store_or_error(const Callback& cb) {
    auto h = history();
    if (h == nullptr) {
        cb(json_error(drogon::k503ServiceUnavailable, "history store not initialised"));
    }
    return h;
}

Json::Value user_json(const UserRecord& u) {
    Json::Value v;
    v["id"] = Json::Value::Int64{u.id};
    v["username"] = u.username;
    v["fullName"] = u.full_name;
    v["email"] = u.email;
    v["role"] = role_name(u.role);
    v["active"] = u.active;
    v["createdMs"] = Json::Value::Int64{u.created_ms};
    if (u.last_login_ms > 0) {
        v["lastLoginMs"] = Json::Value::Int64{u.last_login_ms};
    }
    return v;
}

void no_content(const Callback& cb) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k204NoContent);
    cb(resp);
}

std::int64_t parse_id(const std::string& s) {
    try {
        std::size_t used = 0;
        const auto n = std::stoll(s, &used);
        return used == s.size() ? n : -1;
    } catch (...) {
        return -1;
    }
}

}  // namespace

class AdminController : public drogon::HttpController<AdminController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AdminController::alerts, "/api/alerts", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(AdminController::ack, "/api/alerts/{id}/ack", drogon::Post,
                  "gate::dash_api::OperatorFilter");
    ADD_METHOD_TO(AdminController::ack_all, "/api/alerts/ack-all", drogon::Post,
                  "gate::dash_api::OperatorFilter");
    ADD_METHOD_TO(AdminController::gates, "/api/gates", drogon::Get, "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(AdminController::configure_gate, "/api/gates/{gate_id}", drogon::Put,
                  "gate::dash_api::AdminFilter");
    ADD_METHOD_TO(AdminController::list_users, "/api/users", drogon::Get,
                  "gate::dash_api::AdminFilter");
    ADD_METHOD_TO(AdminController::create_user, "/api/users", drogon::Post,
                  "gate::dash_api::AdminFilter");
    ADD_METHOD_TO(AdminController::update_user, "/api/users/{id}", drogon::Put,
                  "gate::dash_api::AdminFilter");
    ADD_METHOD_TO(AdminController::delete_user, "/api/users/{id}", drogon::Delete,
                  "gate::dash_api::AdminFilter");
    ADD_METHOD_TO(AdminController::settings, "/api/settings", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(AdminController::update_settings, "/api/settings", drogon::Put,
                  "gate::dash_api::AdminFilter");
    METHOD_LIST_END

    // ---- alerts ---------------------------------------------------------
    void alerts(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = store_or_error(cb)) {
            const bool unread = req->getParameter("unread") == "1";
            const auto limit = parse_id(req->getParameter("limit"));
            const auto offset = parse_id(req->getParameter("offset"));
            auto out = h->alerts(unread, limit > 0 ? static_cast<std::uint32_t>(limit) : 50,
                                 offset > 0 ? static_cast<std::uint32_t>(offset) : 0);
            out["unread"] = h->unread_alerts();
            cb(drogon::HttpResponse::newHttpJsonResponse(out));
        }
    }
    void ack(const drogon::HttpRequestPtr& req, Callback&& cb, const std::string& id) const {
        if (const auto h = store_or_error(cb)) {
            if (!h->acknowledge_alert(parse_id(id), request_user(req))) {
                cb(json_error(drogon::k404NotFound, "no such alert"));
                return;
            }
            no_content(cb);
        }
    }
    void ack_all(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = store_or_error(cb)) {
            Json::Value out;
            out["acknowledged"] = h->acknowledge_all(request_user(req));
            cb(drogon::HttpResponse::newHttpJsonResponse(out));
        }
    }

    // ---- gates ----------------------------------------------------------
    void gates(const drogon::HttpRequestPtr&, Callback&& cb) const {
        if (const auto h = store_or_error(cb)) {
            cb(drogon::HttpResponse::newHttpJsonResponse(h->gates()));
        }
    }
    void configure_gate(const drogon::HttpRequestPtr& req, Callback&& cb,
                        std::string gate_id) const {
        const auto h = store_or_error(cb);
        if (h == nullptr) {
            return;
        }
        const auto body = req->getJsonObject();
        if (body == nullptr) {
            cb(json_error(drogon::k400BadRequest, "body must be JSON"));
            return;
        }
        std::string error;
        if (!h->configure_gate(gate_id, (*body).get("name", "").asString(),
                               (*body).get("location", "").asString(),
                               (*body).get("direction", "both").asString(), error)) {
            cb(json_error(drogon::k400BadRequest, error));
            return;
        }
        cb(drogon::HttpResponse::newHttpJsonResponse(h->gates()));
    }

    // ---- users ----------------------------------------------------------
    void list_users(const drogon::HttpRequestPtr&, Callback&& cb) const {
        if (const auto h = store_or_error(cb)) {
            Json::Value out(Json::arrayValue);
            for (const auto& u : h->users()) {
                out.append(user_json(u));
            }
            cb(drogon::HttpResponse::newHttpJsonResponse(out));
        }
    }

    // Shared body → record mapping for create and update.
    static bool apply_body(const Json::Value& body, UserRecord& u, bool creating,
                           std::string& error) {
        if (body.isMember("username")) {
            u.username = body["username"].asString();
        }
        if (body.isMember("fullName")) {
            u.full_name = body["fullName"].asString();
        }
        if (body.isMember("email")) {
            u.email = body["email"].asString();
        }
        if (body.isMember("active")) {
            u.active = body["active"].asBool();
        }
        if (body.isMember("role")) {
            const auto r = role_from_name(body["role"].asString());
            if (!r) {
                error = "role must be viewer, operator or admin";
                return false;
            }
            u.role = *r;
        }
        u.password_hash.clear();
        if (const auto pw = body.get("password", "").asString(); !pw.empty()) {
            if (pw.size() < 10) {
                error = "passwords need at least 10 characters";
                return false;
            }
            u.password_hash = AuthService::hash_password(pw);
        } else if (creating) {
            error = "a new user needs a password";
            return false;
        }
        return true;
    }

    void create_user(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        const auto h = store_or_error(cb);
        const auto body = req->getJsonObject();
        if (h == nullptr) {
            return;
        }
        if (body == nullptr) {
            cb(json_error(drogon::k400BadRequest, "body must be JSON"));
            return;
        }
        UserRecord u;
        std::string error;
        if (!apply_body(*body, u, true, error) || !h->save_user(u, error)) {
            cb(json_error(drogon::k400BadRequest, error));
            return;
        }
        auto resp = drogon::HttpResponse::newHttpJsonResponse(user_json(u));
        resp->setStatusCode(drogon::k201Created);
        cb(resp);
    }

    void update_user(const drogon::HttpRequestPtr& req, Callback&& cb,
                     const std::string& id) const {
        const auto h = store_or_error(cb);
        const auto body = req->getJsonObject();
        if (h == nullptr) {
            return;
        }
        auto u = h->user_by_id(parse_id(id));
        if (!u) {
            cb(json_error(drogon::k404NotFound, "no such user"));
            return;
        }
        if (body == nullptr) {
            cb(json_error(drogon::k400BadRequest, "body must be JSON"));
            return;
        }
        std::string error;
        if (!apply_body(*body, *u, false, error) || !h->save_user(*u, error)) {
            cb(json_error(drogon::k400BadRequest, error));
            return;
        }
        cb(drogon::HttpResponse::newHttpJsonResponse(user_json(*u)));
    }

    void delete_user(const drogon::HttpRequestPtr& req, Callback&& cb,
                     const std::string& id) const {
        if (const auto h = store_or_error(cb)) {
            const auto me = h->user_by_name(request_user(req));
            if (me && me->id == parse_id(id)) {
                cb(json_error(drogon::k400BadRequest, "you cannot delete your own account"));
                return;
            }
            std::string error;
            if (!h->delete_user(parse_id(id), error)) {
                cb(json_error(
                    error == "no such user" ? drogon::k404NotFound : drogon::k400BadRequest,
                    error));
                return;
            }
            no_content(cb);
        }
    }

    // ---- settings -------------------------------------------------------
    void settings(const drogon::HttpRequestPtr&, Callback&& cb) const {
        if (const auto h = store_or_error(cb)) {
            Json::Value out;
            out["retentionDays"] = std::stoi(h->setting("retention_days", "90"));
            out["sessionTimeoutMinutes"] = std::stoi(h->setting("session_timeout_minutes", "720"));
            cb(drogon::HttpResponse::newHttpJsonResponse(out));
        }
    }
    void update_settings(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        const auto h = store_or_error(cb);
        const auto body = req->getJsonObject();
        if (h == nullptr) {
            return;
        }
        if (body == nullptr) {
            cb(json_error(drogon::k400BadRequest, "body must be JSON"));
            return;
        }
        if (body->isMember("retentionDays")) {
            const int d = (*body)["retentionDays"].asInt();
            if (d < 1 || d > 3650) {
                cb(json_error(drogon::k400BadRequest, "retentionDays must be 1–3650"));
                return;
            }
            h->set_setting("retention_days", std::to_string(d));
        }
        if (body->isMember("sessionTimeoutMinutes")) {
            const int m = (*body)["sessionTimeoutMinutes"].asInt();
            if (m < 5 || m > 10'080) {
                cb(json_error(drogon::k400BadRequest, "sessionTimeoutMinutes must be 5–10080"));
                return;
            }
            h->set_setting("session_timeout_minutes", std::to_string(m));
        }
        settings(req, std::move(cb));
    }
};

}  // namespace gate::dash_api
