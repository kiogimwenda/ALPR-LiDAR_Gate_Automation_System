// status_controller.cpp — GET /api/status.
//
// The snapshot a dashboard paints before (or without) the WebSocket:
// latest telemetry per gate from the event cache, plus stream health
// so the UI can badge staleness instead of presenting dead data as
// live.

#include <drogon/HttpSimpleController.h>

#include "dash_api/event_stream.hpp"
#include "dash_api/grpc_bridge.hpp"

namespace gate::dash_api {

class StatusController : public drogon::HttpSimpleController<StatusController> {
public:
    PATH_LIST_BEGIN
    PATH_ADD("/api/status", drogon::Get);
    PATH_LIST_END

    void asyncHandleHttpRequest(
        const drogon::HttpRequestPtr& /*req*/,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback) override {
        const auto stream = event_stream();
        Json::Value body;
        if (stream != nullptr) {
            body = stream->cache().status_snapshot();
            body["streamConnected"] = stream->connected();
        } else {
            body["gates"] = Json::Value(Json::objectValue);
            body["lastEventId"] = 0;
            body["streamConnected"] = false;
        }
        const auto b = bridge();
        body["upstream"] = (b != nullptr) && b->upstream_ready(std::chrono::milliseconds(0));
        callback(drogon::HttpResponse::newHttpJsonResponse(body));
    }
};

}  // namespace gate::dash_api
