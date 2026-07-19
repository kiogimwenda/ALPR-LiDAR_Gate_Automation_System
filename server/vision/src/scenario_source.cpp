// scenario_source.cpp — fail-closed scenario parsing + paced replay.

#include "vision/scenario_source.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace gate::vision {

namespace {

using nlohmann::json;

// Every rejection funnels through here so the message always carries
// the JSON path of the offending value — "event[3].plates[0].ocr_conf"
// beats "bad scenario" when a 200-line script fails at 2 a.m.
[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw std::runtime_error("scenario: " + where + ": " + what);
}

const json& require(const json& obj, const char* key, const std::string& where) {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        fail(where, std::string("missing required field '") + key + "'");
    }
    return *it;
}

float require_confidence(const json& obj, const char* key, const std::string& where) {
    const json& v = require(obj, key, where);
    if (!v.is_number()) {
        fail(where + "." + key, "must be a number");
    }
    const float f = v.get<float>();
    // Out-of-range confidence means the producer and this schema
    // disagree about units — refusing beats feeding the fusion ladder
    // garbage that could authorize a gate open.
    if (f < 0.0f || f > 1.0f) {
        fail(where + "." + key, "confidence must be in [0, 1], got " + v.dump());
    }
    return f;
}

VehicleClass parse_vehicle_class(const json& v, const std::string& where) {
    if (!v.is_string()) {
        fail(where, "must be a string");
    }
    const std::string s = v.get<std::string>();
    if (s == "unknown")
        return VehicleClass::kUnknown;
    if (s == "pedestrian")
        return VehicleClass::kPedestrian;
    if (s == "bicycle")
        return VehicleClass::kBicycle;
    if (s == "motorcycle")
        return VehicleClass::kMotorcycle;
    if (s == "sedan")
        return VehicleClass::kSedan;
    if (s == "suv")
        return VehicleClass::kSuv;
    if (s == "pickup")
        return VehicleClass::kPickup;
    if (s == "van")
        return VehicleClass::kVan;
    if (s == "truck")
        return VehicleClass::kTruck;
    fail(where, "unknown vehicle class '" + s + "'");
}

PlateObservation parse_plate(const json& p, const std::string& where) {
    if (!p.is_object()) {
        fail(where, "must be an object");
    }
    PlateObservation out;
    const json& text = require(p, "text", where);
    if (!text.is_string() || text.get<std::string>().empty()) {
        fail(where + ".text", "must be a non-empty string");
    }
    out.text = text.get<std::string>();
    out.detection_conf = require_confidence(p, "detection_conf", where);
    out.ocr_conf = require_confidence(p, "ocr_conf", where);
    if (const auto it = p.find("box"); it != p.end()) {
        const std::string bw = where + ".box";
        if (!it->is_object()) {
            fail(bw, "must be an object");
        }
        out.box.x = it->value("x", 0u);
        out.box.y = it->value("y", 0u);
        out.box.width = it->value("width", 0u);
        out.box.height = it->value("height", 0u);
    }
    out.country_hint = p.value("country_hint", std::string{});
    out.raw_ocr = p.value("raw_ocr", std::string{});
    return out;
}

VehicleObservation parse_vehicle(const json& v, const std::string& where) {
    if (!v.is_object()) {
        fail(where, "must be an object");
    }
    VehicleObservation out;
    out.vehicle_class = parse_vehicle_class(require(v, "class", where), where + ".class");
    out.confidence = require_confidence(v, "confidence", where);
    if (const auto it = v.find("box"); it != v.end()) {
        const std::string bw = where + ".box";
        if (!it->is_object()) {
            fail(bw, "must be an object");
        }
        out.box.center_x = it->value("center_x", 0.0f);
        out.box.center_y = it->value("center_y", 0.0f);
        out.box.center_z = it->value("center_z", 0.0f);
        out.box.length = it->value("length", 0.0f);
        out.box.width = it->value("width", 0.0f);
        out.box.height = it->value("height", 0.0f);
        out.box.yaw_radians = it->value("yaw_radians", 0.0f);
    }
    out.track_id = v.value("track_id", 0u);
    out.point_count = v.value("point_count", 0u);
    return out;
}

ScenarioEvent parse_event(const json& e, const std::string& where) {
    if (!e.is_object()) {
        fail(where, "must be an object");
    }
    ScenarioEvent out;
    const json& offset = require(e, "offset_ms", where);
    if (!offset.is_number_integer()) {
        fail(where + ".offset_ms", "must be an integer");
    }
    const auto ms = offset.get<std::int64_t>();
    if (ms < 0) {
        fail(where + ".offset_ms", "must be >= 0, got " + offset.dump());
    }
    out.offset = std::chrono::milliseconds{ms};
    if (const auto it = e.find("plates"); it != e.end()) {
        if (!it->is_array()) {
            fail(where + ".plates", "must be an array");
        }
        for (std::size_t i = 0; i < it->size(); ++i) {
            out.plates.push_back(
                parse_plate((*it)[i], where + ".plates[" + std::to_string(i) + "]"));
        }
    }
    if (const auto it = e.find("vehicles"); it != e.end()) {
        if (!it->is_array()) {
            fail(where + ".vehicles", "must be an array");
        }
        for (std::size_t i = 0; i < it->size(); ++i) {
            out.vehicles.push_back(
                parse_vehicle((*it)[i], where + ".vehicles[" + std::to_string(i) + "]"));
        }
    }
    return out;
}

}  // namespace

std::chrono::steady_clock::time_point SteadyClock::now() {
    return std::chrono::steady_clock::now();
}

std::chrono::system_clock::time_point SteadyClock::wall_now() {
    return std::chrono::system_clock::now();
}

void SteadyClock::sleep_until(std::chrono::steady_clock::time_point tp) {
    std::this_thread::sleep_until(tp);
}

ScenarioSource::ScenarioSource(std::vector<ScenarioEvent> events, unsigned loops,
                               std::shared_ptr<Clock> clock)
    : events_(std::move(events)), loops_(loops), clock_(std::move(clock)) {}

ScenarioSource ScenarioSource::from_json(const std::string& json_text,
                                         std::shared_ptr<Clock> clock) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::parse_error& e) {
        // Re-throw as the library's one error type; keep the parser's
        // byte offset — it is the fastest route to the typo.
        fail("(document)", std::string("malformed JSON — ") + e.what());
    }
    if (!root.is_object()) {
        fail("(document)", "top level must be an object");
    }

    unsigned loops = 1;
    if (const auto it = root.find("loop"); it != root.end()) {
        if (!it->is_number_integer() || it->get<std::int64_t>() < 1) {
            fail("loop", "must be an integer >= 1, got " + it->dump());
        }
        loops = static_cast<unsigned>(it->get<std::int64_t>());
    }

    // Lvalue on purpose: passing a temporary here trips GCC's
    // -Wdangling-reference heuristic on the returned reference (which
    // really points into `root`, but the compiler can't prove it).
    const std::string doc_where = "(document)";
    const json& events_json = require(root, "events", doc_where);
    if (!events_json.is_array() || events_json.empty()) {
        // An empty scenario is almost certainly a truncated or wrong
        // file — refusing is the fail-closed posture.
        fail("events", "must be a non-empty array");
    }
    std::vector<ScenarioEvent> events;
    events.reserve(events_json.size());
    for (std::size_t i = 0; i < events_json.size(); ++i) {
        events.push_back(parse_event(events_json[i], "events[" + std::to_string(i) + "]"));
    }

    if (!clock) {
        clock = std::make_shared<SteadyClock>();
    }
    return ScenarioSource{std::move(events), loops, std::move(clock)};
}

ScenarioSource ScenarioSource::from_file(const std::filesystem::path& path,
                                         std::shared_ptr<Clock> clock) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("scenario: cannot read file: " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return from_json(ss.str(), std::move(clock));
}

std::optional<ObservationSet> ScenarioSource::next() {
    if (iteration_ >= loops_) {
        return std::nullopt;
    }
    // The iteration epoch is captured on its first next() call — not in
    // the constructor — so time spent between construction and the
    // first pull (channel setup, TLS handshake) never makes the first
    // events fire late or bunched.
    if (!epoch_set_) {
        epoch_ = clock_->now();
        epoch_set_ = true;
    }
    const ScenarioEvent& ev = events_[index_];
    clock_->sleep_until(epoch_ + ev.offset);

    ObservationSet out;
    out.capture_ts = clock_->wall_now();
    out.plates = ev.plates;
    out.vehicles = ev.vehicles;

    if (++index_ == events_.size()) {
        index_ = 0;
        ++iteration_;
        epoch_set_ = false;  // Next iteration re-bases at its own start
    }
    return out;
}

}  // namespace gate::vision
