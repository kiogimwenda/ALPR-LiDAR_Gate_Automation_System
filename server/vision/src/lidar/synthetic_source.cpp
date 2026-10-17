// synthetic_source.cpp — ray casting against ground + vehicle shapes.

#include "vision/lidar/synthetic_source.hpp"

#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <stdexcept>
#include <thread>

namespace gate::vision::lidar {

namespace {

using json = nlohmann::json;

struct Box {
    float cx, cy, yaw;  // footprint centre + heading (gate frame)
    float hl, hw;       // half length / width
    float z0, z1;       // vertical extent
};

// Slab test of ray o + t·d against an oriented box; returns the entry
// distance or +inf.
float hit_box(const Vec3& o, const Vec3& d, const Box& b) {
    const float c = std::cos(-b.yaw), s = std::sin(-b.yaw);
    const float ox = c * (o.x - b.cx) - s * (o.y - b.cy);
    const float oy = s * (o.x - b.cx) + c * (o.y - b.cy);
    const float dx = c * d.x - s * d.y;
    const float dy = s * d.x + c * d.y;
    float tmin = 0.0f, tmax = std::numeric_limits<float>::infinity();
    auto slab = [&](float oo, float dd, float lo, float hi) {
        if (std::abs(dd) < 1e-9f) {
            return oo >= lo && oo <= hi;
        }
        float t1 = (lo - oo) / dd, t2 = (hi - oo) / dd;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        return tmin <= tmax;
    };
    if (!slab(ox, dx, -b.hl, b.hl) || !slab(oy, dy, -b.hw, b.hw) || !slab(o.z, d.z, b.z0, b.z1)) {
        return std::numeric_limits<float>::infinity();
    }
    return tmin > 0.0f ? tmin : std::numeric_limits<float>::infinity();
}

void boxes_for(const SceneObject& ob, std::vector<Box>& out) {
    if (ob.shape == SceneObject::Shape::kBox) {
        out.push_back({ob.x, ob.y, ob.yaw, ob.length / 2, ob.width / 2, 0.0f, ob.height});
        return;
    }
    // Car: full-length body (ground clearance 0.18 m up to 0.62 of the
    // height) plus a narrower cabin over the middle-rear half.
    out.push_back({ob.x, ob.y, ob.yaw, ob.length / 2, ob.width / 2, 0.18f, 0.62f * ob.height});
    const float back = -0.08f * ob.length;  // cabin sits slightly rearward
    out.push_back({ob.x + std::cos(ob.yaw) * back, ob.y + std::sin(ob.yaw) * back, ob.yaw,
                   0.26f * ob.length, 0.45f * ob.width, 0.62f * ob.height, ob.height});
}

float num(const json& j, const char* key, float dflt, const std::string& where) {
    if (!j.contains(key)) {
        return dflt;
    }
    if (!j.at(key).is_number()) {
        throw std::runtime_error(where + "." + key + " must be a number");
    }
    return j.at(key).get<float>();
}

}  // namespace

SceneConfig load_scene(const std::filesystem::path& path) {
    std::ifstream in{path};
    if (!in) {
        throw std::runtime_error("synthetic lidar: cannot open scene " + path.string());
    }
    json j;
    try {
        in >> j;
    } catch (const json::exception& e) {
        throw std::runtime_error("synthetic lidar: " + path.string() + ": " + e.what());
    }
    SceneConfig c;
    if (j.contains("mount")) {
        const auto& m = j.at("mount");
        c.mount = {num(m, "x", 0, "mount"),     num(m, "y", 0, "mount"),
                   num(m, "z", 0, "mount"),     num(m, "roll", 0, "mount"),
                   num(m, "pitch", 0, "mount"), num(m, "yaw", 0, "mount")};
    }
    c.points_per_second = num(j, "points_per_second", c.points_per_second, "scene");
    c.rate_hz = num(j, "rate_hz", c.rate_hz, "scene");
    c.noise_m = num(j, "noise_m", c.noise_m, "scene");
    c.duration_s = num(j, "duration_s", c.duration_s, "scene");
    c.seed = static_cast<std::uint32_t>(num(j, "seed", 1, "scene"));
    c.realtime = j.value("realtime", true);
    if (c.rate_hz <= 0 || c.points_per_second <= 0) {
        throw std::runtime_error("synthetic lidar: rate_hz and points_per_second must be > 0");
    }
    if (j.contains("objects")) {
        int idx = 0;
        for (const auto& o : j.at("objects")) {
            const std::string w = "objects[" + std::to_string(idx++) + "]";
            SceneObject ob;
            const std::string shape = o.value("shape", "car");
            if (shape == "car") {
                ob.shape = SceneObject::Shape::kCar;
            } else if (shape == "box") {
                ob.shape = SceneObject::Shape::kBox;
            } else {
                throw std::runtime_error("synthetic lidar: " + w + ".shape must be car|box");
            }
            ob.length = num(o, "length", ob.length, w);
            ob.width = num(o, "width", ob.width, w);
            ob.height = num(o, "height", ob.height, w);
            ob.x = num(o, "x", 0, w);
            ob.y = num(o, "y", 0, w);
            ob.yaw = num(o, "yaw", 0, w);
            ob.vx = num(o, "vx", 0, w);
            ob.vy = num(o, "vy", 0, w);
            ob.stop_after_s = num(o, "stop_after_s", -1, w);
            if (ob.length <= 0 || ob.width <= 0 || ob.height <= 0) {
                throw std::runtime_error("synthetic lidar: " + w + " dimensions must be > 0");
            }
            c.objects.push_back(ob);
        }
    }
    return c;
}

SyntheticSource::SyntheticSource(SceneConfig cfg)
    : cfg_(std::move(cfg)),
      tf_(cfg_.mount),
      rng_(cfg_.seed),
      t0_(cfg_.start.value_or(std::chrono::system_clock::now())),
      steady_t0_(std::chrono::steady_clock::now()) {}

std::vector<SceneObject> SyntheticSource::objects_at(float t_s) const {
    std::vector<SceneObject> out = cfg_.objects;
    for (auto& o : out) {
        const float moving = o.stop_after_s >= 0 ? std::min(t_s, o.stop_after_s) : t_s;
        o.x += o.vx * moving;
        o.y += o.vy * moving;
    }
    return out;
}

std::optional<PointCloud> SyntheticSource::next() {
    const double period = 1.0 / static_cast<double>(cfg_.rate_hz);
    const double t = static_cast<double>(batch_) * period;
    if (cfg_.duration_s > 0 &&
        batch_ >= static_cast<std::uint64_t>(
                      std::llround(static_cast<double>(cfg_.duration_s) * cfg_.rate_hz))) {
        return std::nullopt;
    }
    if (cfg_.realtime) {
        std::this_thread::sleep_until(
            steady_t0_ +
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(t)));
    }
    ++batch_;

    std::vector<Box> boxes;
    for (const auto& o : objects_at(static_cast<float>(t))) {
        boxes_for(o, boxes);
    }

    PointCloud pc;
    pc.stamp = t0_ + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                         std::chrono::duration<double>(t));
    const auto rays = static_cast<std::size_t>(cfg_.points_per_second * period);
    pc.points.reserve(rays);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::normal_distribution<float> noise(0.0f, cfg_.noise_m);
    const Vec3 o = tf_.translation();
    for (std::size_t i = 0; i < rays; ++i) {
        // Uniform over the upper hemisphere of the sensor frame.
        const float z = uni(rng_);
        const float phi = 2.0f * std::numbers::pi_v<float> * uni(rng_);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const Vec3 ds{r * std::cos(phi), r * std::sin(phi), z};
        const Vec3 d = tf_.rotate(ds.x, ds.y, ds.z);
        float best = std::numeric_limits<float>::infinity();
        if (d.z < -1e-6f) {
            best = -o.z / d.z;  // ground plane z = 0
        }
        for (const Box& b : boxes) {
            best = std::min(best, hit_box(o, d, b));
        }
        if (!(best < cfg_.range_max)) {
            continue;  // sky or out of range: no return
        }
        const float range = best + noise(rng_);
        if (range <= 0.05f) {
            continue;
        }
        // Sensor-frame point along the sensor-frame ray direction.
        pc.points.push_back({ds.x * range, ds.y * range, ds.z * range, 100.0f});
    }
    return pc;
}

std::string SyntheticSource::describe() const {
    return "synthetic lidar (" + std::to_string(cfg_.objects.size()) + " objects, " +
           std::to_string(static_cast<int>(cfg_.points_per_second)) + " pts/s)";
}

}  // namespace gate::vision::lidar
