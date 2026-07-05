// grpc_http.hpp — shared helpers for REST controllers.
//
// Maps upstream gRPC outcomes onto HTTP semantics and standardises
// the error body ({"error": …}). Lives with the controllers (compiled
// into the executable) because it depends on Drogon.

#pragma once

#include <drogon/HttpResponse.h>

#include "dash_api/grpc_bridge.hpp"

namespace gate::dash_api {

inline drogon::HttpStatusCode http_status(grpc::StatusCode code) {
    switch (code) {
        case grpc::StatusCode::OK:
            return drogon::k200OK;
        case grpc::StatusCode::INVALID_ARGUMENT:
            return drogon::k400BadRequest;
        case grpc::StatusCode::NOT_FOUND:
            return drogon::k404NotFound;
        case grpc::StatusCode::ALREADY_EXISTS:
            return drogon::k409Conflict;
        case grpc::StatusCode::PERMISSION_DENIED:
        case grpc::StatusCode::UNAUTHENTICATED:
            return drogon::k403Forbidden;
        case grpc::StatusCode::DEADLINE_EXCEEDED:
            return drogon::k504GatewayTimeout;
        case grpc::StatusCode::UNAVAILABLE:
            return drogon::k503ServiceUnavailable;
        case grpc::StatusCode::UNIMPLEMENTED:
            return drogon::k501NotImplemented;
        default:
            return drogon::k502BadGateway;  // upstream failed in a way we can't refine
    }
}

inline drogon::HttpResponsePtr json_error(drogon::HttpStatusCode status,
                                          const std::string& message) {
    Json::Value body;
    body["error"] = message;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    return resp;
}

// 503 with a hint when main never wired the bridge (cannot happen in
// the shipped binary; belt-and-braces for future embeddings).
inline std::shared_ptr<GrpcBridge> bridge_or_error(
    const std::function<void(const drogon::HttpResponsePtr&)>& callback) {
    auto b = bridge();
    if (b == nullptr) {
        callback(json_error(drogon::k503ServiceUnavailable, "gRPC bridge not initialised"));
    }
    return b;
}

}  // namespace gate::dash_api
