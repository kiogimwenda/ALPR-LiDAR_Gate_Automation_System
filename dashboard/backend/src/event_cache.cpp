// event_cache.cpp — ring + per-gate state + fan-out.

#include "dash_api/event_cache.hpp"

#include "dash_api/json_mapping.hpp"

namespace gate::dash_api {

namespace {

std::string compact(const Json::Value& v) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString(b, v);
}

}  // namespace

void EventCache::ingest(const gate::v1::DashboardEvent& ev) {
    const Json::Value mapped = to_json(ev);
    const std::string frame = compact(mapped);

    // Raw listeners (the HistoryStore) run outside the cache lock: they
    // do database I/O and must never stall WebSocket fan-out readers.
    std::vector<Listener> listeners;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        listeners = listeners_;
    }
    for (const auto& l : listeners) {
        l(ev);
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    // Event ids are monotonic per gate-server *boot*. A new stream asks
    // for ids after our cursor, so if its first event is at or below the
    // cursor the server restarted and counts from 1 again: follow it
    // down, or every later reconnect would ask for "after 5000" and miss
    // the new server's replay. Within a stream the cursor only rises.
    if (stream_fresh_ && ev.event_id() != 0 && ev.event_id() <= last_event_id_) {
        last_event_id_ = ev.event_id();
    } else if (ev.event_id() > last_event_id_) {
        last_event_id_ = ev.event_id();
    }
    stream_fresh_ = false;
    ring_.push_back(frame);
    while (ring_.size() > ring_capacity_) {
        ring_.pop_front();
    }
    if (ev.payload_case() == gate::v1::DashboardEvent::kTelemetry) {
        Json::Value g;
        g["telemetry"] = mapped["telemetry"];
        if (mapped.isMember("event")) {
            g["lastEvent"] = mapped["event"];
        }
        gates_[ev.telemetry().gate_id()] = g;
    }
    for (const auto& [id, sink] : sinks_) {
        sink(frame);
    }
}

std::uint64_t EventCache::add_sink(Sink sink) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto id = next_sink_id_++;
    sinks_[id] = std::move(sink);
    return id;
}

void EventCache::remove_sink(std::uint64_t id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    sinks_.erase(id);
}

std::size_t EventCache::sink_count() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return sinks_.size();
}

std::vector<std::string> EventCache::recent_frames() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return {ring_.begin(), ring_.end()};
}

Json::Value EventCache::status_snapshot() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    Json::Value v;
    Json::Value gates(Json::objectValue);
    for (const auto& [id, g] : gates_) {
        gates[id] = g;
    }
    v["gates"] = gates;
    v["lastEventId"] = Json::Value::UInt64{last_event_id_};
    return v;
}

std::uint64_t EventCache::last_event_id() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return last_event_id_;
}

}  // namespace gate::dash_api
