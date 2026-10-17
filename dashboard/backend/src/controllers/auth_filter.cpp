// auth_filter.cpp — Bearer-token + role gates on the API.
//
// Three filters, one rule each (roles are cumulative):
//
//   AuthFilter      any signed-in user (viewer and up): reports, history,
//                   search, alerts list, gate list
//   OperatorFilter  operator and up: gate commands, alert acknowledgement,
//                   allowlist management
//   AdminFilter     admin only: users, gate configuration, settings
//
// Attached per-route rather than globally: /api/health, /api/status and
// the event WebSocket stay open — the guard-booth view must survive an
// expired session.
//
// Auth is *required* once either an admin credential is configured
// (--admin-password-hash) or the user table holds anyone. With neither,
// every request passes as an anonymous admin and main() says so loudly
// at startup (dev/tests). Like the controllers, this file must compile
// into the executable: Drogon registers filters via global constructors
// and the linker drops unreferenced archive members.

#include <drogon/HttpFilter.h>

#include "dash_api/auth.hpp"
#include "dash_api/history_store.hpp"
#include "grpc_http.hpp"
#include "request_identity.hpp"

namespace gate::dash_api {

bool auth_required() {
    const auto a = auth();
    if (a != nullptr && a->enabled()) {
        return true;
    }
    const auto h = history();
    return h != nullptr && h->user_count() > 0;
}

namespace {

void gate_request(const drogon::HttpRequestPtr& req, Role minimum, drogon::FilterCallback&& reject,
                  drogon::FilterChainCallback&& accept) {
    if (!auth_required()) {
        req->attributes()->insert("auth_user", std::string{"anonymous"});
        req->attributes()->insert("auth_role", std::string{"admin"});
        accept();
        return;
    }
    const auto a = auth();
    const auto& header = req->getHeader("authorization");
    static constexpr std::string_view kBearer = "Bearer ";
    std::string subject;
    std::string role_text;
    if (a == nullptr || header.rfind(kBearer, 0) != 0 ||
        !a->verify_token(header.substr(kBearer.size()), subject, role_text)) {
        reject(json_error(drogon::k401Unauthorized, "authentication required"));
        return;
    }
    // A token outlives a deactivation or demotion only until it is
    // checked against the user table here.
    if (const auto h = history(); h != nullptr && h->user_count() > 0) {
        const auto u = h->user_by_name(subject);
        if (!u || !u->active) {
            reject(json_error(drogon::k401Unauthorized, "account disabled or removed"));
            return;
        }
        role_text = role_name(u->role);
    }
    const Role role = role_from_name(role_text).value_or(Role::kViewer);
    if (static_cast<int>(role) < static_cast<int>(minimum)) {
        reject(json_error(drogon::k403Forbidden,
                          std::string{"this action needs the "} + role_name(minimum) + " role"));
        return;
    }
    req->attributes()->insert("auth_user", subject);
    req->attributes()->insert("auth_role", std::string{role_name(role)});
    accept();
}

}  // namespace

class AuthFilter : public drogon::HttpFilter<AuthFilter> {
public:
    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& reject,
                  drogon::FilterChainCallback&& accept) override {
        gate_request(req, Role::kViewer, std::move(reject), std::move(accept));
    }
};

class OperatorFilter : public drogon::HttpFilter<OperatorFilter> {
public:
    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& reject,
                  drogon::FilterChainCallback&& accept) override {
        gate_request(req, Role::kOperator, std::move(reject), std::move(accept));
    }
};

class AdminFilter : public drogon::HttpFilter<AdminFilter> {
public:
    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& reject,
                  drogon::FilterChainCallback&& accept) override {
        gate_request(req, Role::kAdmin, std::move(reject), std::move(accept));
    }
};

// Drogon instantiates filters reflectively through DrObject<T>::alloc_,
// a class-template static that the compiler only emits in a TU that
// odr-uses the class. Controllers odr-use themselves via METHOD_LIST's
// route registration; a filter is referenced by name alone, so without
// these lines nothing is emitted and route setup fails at runtime with
// "middleware … not found".
[[maybe_unused]] static const std::string& force_auth_filter = AuthFilter::classTypeName();
[[maybe_unused]] static const std::string& force_operator_filter = OperatorFilter::classTypeName();
[[maybe_unused]] static const std::string& force_admin_filter = AdminFilter::classTypeName();

}  // namespace gate::dash_api
