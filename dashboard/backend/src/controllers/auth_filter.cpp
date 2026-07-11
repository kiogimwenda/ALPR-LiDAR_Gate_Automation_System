// auth_filter.cpp — Bearer-token gate on the admin surface.
//
// Attached per-route (see gates/allowlist controllers) rather than
// globally: monitoring endpoints (/api/health, /api/status, the
// event WebSocket) stay open — the guard-booth view must survive an
// expired admin session — while everything that *changes* the site
// or reads resident PII sits behind the token.
//
// When auth is disabled the filter passes everything through; the
// loud startup warning in main() is the only trace. Like the
// controllers, this file must compile into the executable: Drogon
// registers filters via global constructors and the linker drops
// unreferenced archive members.

#include <drogon/HttpFilter.h>

#include "dash_api/auth.hpp"
#include "grpc_http.hpp"

namespace gate::dash_api {

class AuthFilter : public drogon::HttpFilter<AuthFilter> {
public:
    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& reject,
                  drogon::FilterChainCallback&& accept) override {
        const auto a = auth();
        if (a == nullptr || !a->enabled()) {
            accept();
            return;
        }
        const auto& header = req->getHeader("authorization");
        static constexpr const char* kBearer = "Bearer ";
        std::string subject;
        if (header.rfind(kBearer, 0) == 0 &&
            a->verify_token(header.substr(std::string_view(kBearer).size()), subject)) {
            req->attributes()->insert("auth_user", subject);
            accept();
            return;
        }
        reject(json_error(drogon::k401Unauthorized, "authentication required"));
    }
};

// Drogon instantiates filters reflectively through DrObject<T>::alloc_,
// a class-template static that the compiler only emits in a TU that
// odr-uses the class. Controllers odr-use themselves via METHOD_LIST's
// route registration; a filter is referenced by name alone, so without
// this line nothing is emitted and route setup fails at runtime with
// "middleware gate::dash_api::AuthFilter not found".
[[maybe_unused]] static const std::string& kForceRegistration = AuthFilter::classTypeName();

}  // namespace gate::dash_api
