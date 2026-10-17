// event_cache.hpp — the dashboard's in-memory view of the event stream.
//
// Everything the live half of the backend needs to remember, with no
// gRPC and no Drogon anywhere near it (host-tested in
// tests/dashboard_api/):
//
//   - a ring of the most recent presentation-mapped frames, replayed
//     to every new WebSocket connection so a freshly-opened dashboard
//     paints instantly instead of waiting for the next event;
//   - the latest Telemetry per gate, which is the /api/status answer;
//   - the sink registry the WebSocket controller fans events into;
//   - the highest event_id seen, which the Subscribe consumer passes
//     back as since_event_id so a reconnect resumes instead of
//     replaying or gapping.
//
// Frames are serialised to compact JSON strings exactly once per
// event, however many sinks are attached.

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <json/json.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "gate_service.pb.h"

namespace gate::dash_api {

class EventCache {
public:
    using Sink = std::function<void(const std::string& frame)>;
    // Sees every event before it is mapped for the UI (persistence).
    using Listener = std::function<void(const gate::v1::DashboardEvent& ev)>;

    explicit EventCache(std::size_t ring_capacity = 256) : ring_capacity_(ring_capacity) {}

    // Map, remember, fan out. Sinks run inline under the cache lock —
    // WebSocket sends are queue-posts (trantor), not blocking I/O.
    void ingest(const gate::v1::DashboardEvent& ev);

    // Register before the stream starts; listeners are never removed.
    void add_listener(Listener l) {
        const std::lock_guard<std::mutex> lock(mutex_);
        listeners_.push_back(std::move(l));
    }

    // Called by the Subscribe consumer at every (re)connect, before the
    // first event of the new stream (see the cursor rule in ingest()).
    void begin_stream() {
        const std::lock_guard<std::mutex> lock(mutex_);
        stream_fresh_ = true;
    }

    std::uint64_t add_sink(Sink sink);
    void remove_sink(std::uint64_t id);
    [[nodiscard]] std::size_t sink_count() const;

    // Ring copy, oldest first — the new-connection replay.
    [[nodiscard]] std::vector<std::string> recent_frames() const;

    // {"gates": {"<id>": {"telemetry": …, "lastEvent": {ts…}}},
    //  "lastEventId": …} — /api/status merges in stream health.
    [[nodiscard]] Json::Value status_snapshot() const;

    [[nodiscard]] std::uint64_t last_event_id() const;

private:
    std::size_t ring_capacity_;
    mutable std::mutex mutex_;
    std::deque<std::string> ring_;
    std::map<std::string, Json::Value> gates_;  // gate_id → {telemetry, lastEvent}
    std::map<std::uint64_t, Sink> sinks_;
    std::vector<Listener> listeners_;
    std::uint64_t next_sink_id_ = 1;
    std::uint64_t last_event_id_ = 0;
    bool stream_fresh_ = false;
};

}  // namespace gate::dash_api
