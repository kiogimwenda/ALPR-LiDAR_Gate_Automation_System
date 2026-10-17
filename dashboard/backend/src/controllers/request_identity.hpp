// request_identity.hpp — who is calling, as established by the filters.

#pragma once

#include <drogon/HttpRequest.h>
#include <string>

namespace gate::dash_api {

// True when an admin credential is configured or any user exists
// (auth_filter.cpp).
bool auth_required();

// The signed-in username ("anonymous" when auth is off; "" on an open
// route nobody signed in to).
inline std::string request_user(const drogon::HttpRequestPtr& req) {
    const auto& attrs = req->attributes();
    return attrs->find("auth_user") ? attrs->get<std::string>("auth_user") : std::string{};
}

inline std::string request_role(const drogon::HttpRequestPtr& req) {
    const auto& attrs = req->attributes();
    return attrs->find("auth_role") ? attrs->get<std::string>("auth_role") : std::string{};
}

}  // namespace gate::dash_api
