// events_ws_controller.cpp — /ws/events: the live feed.
//
// Thin on purpose: attach → replay the ring (a fresh dashboard paints
// instantly) → register a sink that forwards each frame; detach →
// remove the sink. All fan-out policy lives in EventCache where it is
// host-tested; trantor's send() is thread-safe queue-posting, so
// calling it from the EventStream thread inside the cache lock is
// non-blocking.

#include <drogon/WebSocketController.h>

#include "dash_api/event_stream.hpp"

namespace gate::dash_api {

class EventsWsController : public drogon::WebSocketController<EventsWsController> {
public:
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/events");
    WS_PATH_LIST_END

    void handleNewConnection(const drogon::HttpRequestPtr& /*req*/,
                             const drogon::WebSocketConnectionPtr& conn) override {
        const auto stream = event_stream();
        if (stream == nullptr) {
            conn->shutdown(drogon::CloseCode::kUnexpectedCondition, "event stream not initialised");
            return;
        }
        for (const auto& frame : stream->cache().recent_frames()) {
            conn->send(frame);
        }
        const auto sink_id = stream->cache().add_sink(
            [weak = std::weak_ptr<drogon::WebSocketConnection>(conn)](const std::string& frame) {
                if (const auto c = weak.lock(); c != nullptr && c->connected()) {
                    c->send(frame);
                }
            });
        conn->setContext(std::make_shared<std::uint64_t>(sink_id));
    }

    void handleNewMessage(const drogon::WebSocketConnectionPtr& conn, std::string&& message,
                          const drogon::WebSocketMessageType& type) override {
        // The feed is one-way; answer pings so naive keepalives work.
        if (type == drogon::WebSocketMessageType::Text && message == "ping") {
            conn->send("pong");
        }
    }

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override {
        const auto stream = event_stream();
        const auto sink_id = conn->getContext<std::uint64_t>();
        if (stream != nullptr && sink_id != nullptr) {
            stream->cache().remove_sink(*sink_id);
        }
    }
};

}  // namespace gate::dash_api
