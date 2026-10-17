// auth_controller.cpp — sign-in and the signed-in user's own account.
//
//   POST /api/auth/login     {"username","password"} → 200 {"token","user","role",
//                            "fullName","expiresInMin"} or 401
//   GET  /api/auth/me        → {"user","role","fullName","email","authRequired"}
//   POST /api/auth/password  {"current","new"} → 204 (own password)
//
// Users live in the HistoryStore's user table (roles viewer / operator /
// admin). main() seeds that table with the --admin-password-hash
// credential on first start, so a site upgrading from the single-admin
// backend keeps signing in with the same password. Wrong user and wrong
// password are the same 401 — the endpoint never confirms which half was
// right — and a disabled account is indistinguishable from a wrong one.

#include <drogon/HttpController.h>

#include "dash_api/auth.hpp"
#include "dash_api/history_store.hpp"
#include "grpc_http.hpp"
#include "request_identity.hpp"

namespace gate::dash_api {

class AuthController : public drogon::HttpController<AuthController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::login, "/api/auth/login", drogon::Post);
    ADD_METHOD_TO(AuthController::me, "/api/auth/me", drogon::Get, "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(AuthController::change_password, "/api/auth/password", drogon::Post,
                  "gate::dash_api::AuthFilter");
    METHOD_LIST_END

    void login(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
        const auto a = auth();
        if (a == nullptr || !auth_required()) {
            callback(json_error(drogon::k503ServiceUnavailable,
                                "sign-in is not configured on this backend (open access)"));
            return;
        }
        const auto body = req->getJsonObject();
        if (body == nullptr || !(*body)["username"].isString() || !(*body)["password"].isString()) {
            callback(json_error(drogon::k400BadRequest,
                                "body requires string 'username' and 'password'"));
            return;
        }
        const auto username = (*body)["username"].asString();
        const auto password = (*body)["password"].asString();

        std::string role = "admin";
        std::string full_name;
        bool ok = false;
        if (const auto h = history(); h != nullptr && h->user_count() > 0) {
            if (const auto u = h->user_by_name(username);
                u && u->active && AuthService::verify_password(password, u->password_hash)) {
                ok = true;
                role = role_name(u->role);
                full_name = u->full_name;
                h->touch_login(u->id);
            }
        } else {
            ok = a->check_credentials(username, password);
        }
        if (!ok) {
            callback(json_error(drogon::k401Unauthorized, "invalid credentials"));
            return;
        }
        Json::Value out;
        out["token"] = a->issue_token(username, role);
        out["user"] = username;
        out["role"] = role;
        out["fullName"] = full_name;
        out["expiresInMin"] = a->ttl_minutes();
        callback(drogon::HttpResponse::newHttpJsonResponse(out));
    }

    void me(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
        Json::Value out;
        out["user"] = request_user(req);
        out["role"] = request_role(req);
        out["authRequired"] = auth_required();
        if (const auto h = history(); h != nullptr) {
            if (const auto u = h->user_by_name(request_user(req))) {
                out["fullName"] = u->full_name;
                out["email"] = u->email;
                out["id"] = Json::Value::Int64{u->id};
            }
        }
        callback(drogon::HttpResponse::newHttpJsonResponse(out));
    }

    void change_password(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
        const auto h = history();
        const auto body = req->getJsonObject();
        if (h == nullptr || body == nullptr || !(*body)["current"].isString() ||
            !(*body)["new"].isString()) {
            callback(
                json_error(drogon::k400BadRequest, "body requires string 'current' and 'new'"));
            return;
        }
        auto u = h->user_by_name(request_user(req));
        if (!u) {
            callback(json_error(drogon::k404NotFound, "no user account for this session"));
            return;
        }
        if (!AuthService::verify_password((*body)["current"].asString(), u->password_hash)) {
            callback(json_error(drogon::k403Forbidden, "current password is wrong"));
            return;
        }
        const auto next = (*body)["new"].asString();
        if (next.size() < 10) {
            callback(json_error(drogon::k400BadRequest, "use at least 10 characters"));
            return;
        }
        u->password_hash = AuthService::hash_password(next);
        std::string error;
        if (!h->save_user(*u, error)) {
            callback(json_error(drogon::k400BadRequest, error));
            return;
        }
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        callback(resp);
    }
};

}  // namespace gate::dash_api
