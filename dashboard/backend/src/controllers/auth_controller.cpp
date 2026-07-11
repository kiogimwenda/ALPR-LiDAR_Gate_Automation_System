// auth_controller.cpp — POST /api/auth/login (Phase 4.10.2).
//
// Body {"username","password"} → 200 {"token","user","expiresInMin"}
// or 401. Wrong user and wrong password are the same 401 — the
// endpoint doesn't confirm which half was right. When auth is
// disabled (no --admin-password-hash) the endpoint says so with a
// 503 rather than minting tokens nothing will ever check.

#include <drogon/HttpController.h>

#include "dash_api/auth.hpp"
#include "grpc_http.hpp"

namespace gate::dash_api {

class AuthController : public drogon::HttpController<AuthController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::login, "/api/auth/login", drogon::Post);
    METHOD_LIST_END

    void login(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
        const auto a = auth();
        if (a == nullptr || !a->enabled()) {
            callback(json_error(drogon::k503ServiceUnavailable,
                                "admin auth is not configured on this backend"));
            return;
        }
        const auto body = req->getJsonObject();
        if (body == nullptr || !(*body)["username"].isString() || !(*body)["password"].isString()) {
            callback(json_error(drogon::k400BadRequest,
                                "body requires string 'username' and 'password'"));
            return;
        }
        const auto user = (*body)["username"].asString();
        if (!a->check_credentials(user, (*body)["password"].asString())) {
            callback(json_error(drogon::k401Unauthorized, "invalid credentials"));
            return;
        }
        Json::Value out;
        out["token"] = a->issue_token(user);
        out["user"] = user;
        out["expiresInMin"] = a->ttl_minutes();
        callback(drogon::HttpResponse::newHttpJsonResponse(out));
    }
};

}  // namespace gate::dash_api
