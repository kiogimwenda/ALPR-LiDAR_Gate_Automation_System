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

    const std::lock_guard<std::mutex> lock(mutex_);
    if (ev.event_id() > last_event_id_) {
        last_event_id_ = ev.event_id();
    }
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
