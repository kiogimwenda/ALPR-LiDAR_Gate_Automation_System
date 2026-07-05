// health_controller.cpp — GET /api/health.
//
// Answers even when the gate-server is down: `status` reflects this
// backend, `upstream` reflects the gRPC channel. Load balancers and
// the frontend's connection indicator read the same endpoint.

#include <drogon/HttpSimpleController.h>

#include "dash_api/grpc_bridge.hpp"

namespace gate::dash_api {

class HealthController : public drogon::HttpSimpleController<HealthController> {
public:
    PATH_LIST_BEGIN
    PATH_ADD("/api/health", drogon::Get);
    PATH_LIST_END

    void asyncHandleHttpRequest(
        const drogon::HttpRequestPtr& /*req*/,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback) override {
        Json::Value body;
        body["status"] = "ok";
        const auto b = bridge();
        body["upstream"] = (b != nullptr) && b->upstream_ready(std::chrono::milliseconds(0));
        body["upstreamAddr"] = (b != nullptr) ? b->config().server : "";
        body["siteId"] = (b != nullptr) ? b->config().site_id : "";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
        callback(resp);
    }
};

}  // namespace gate::dash_api
