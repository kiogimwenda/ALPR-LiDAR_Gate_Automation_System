// vehicle_detector.cpp — ground fit, BEV clustering, boxes, tracking, classes.

#include "vision/lidar/vehicle_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>

namespace gate::vision::lidar {

namespace {

using Clock = std::chrono::system_clock;

// Membership of `v` in [lo, hi]: 1 inside, Gaussian fall-off outside.
float envelope(float v, float lo, float hi, float sigma) {
    if (v >= lo && v <= hi) {
        return 1.0f;
    }
    const float d = v < lo ? lo - v : v - hi;
    return std::exp(-(d * d) / (sigma * sigma));
}

// Like envelope(), for a dimension that is a lower bound on the truth:
// the sensor sees the near end and the roof, so a short or narrow
// measurement is consistent with a bigger vehicle (only mildly
// penalised, floor `under_floor`), while a measurement *above* the
// envelope is real evidence against the class.
float lower_bound_envelope(float v, float lo, float hi, float sigma, float under_floor) {
    if (v < lo) {
        return std::max(under_floor, envelope(v, lo, hi, sigma));
    }
    return envelope(v, lo, hi, sigma);
}

struct ClassEnvelope {
    VehicleClass cls;
    float l_lo, l_hi, w_lo, w_hi, h_lo, h_hi;
    float prior;  // breaks ties when the dimensions cannot (e.g. partial length)
};

// Dimension envelopes (metres) for the vehicles a Kenyan residential
// gate sees. Height is the best-observed dimension from a pillar-mounted
// sensor (it sees the roof); length and width are often partial (one
// side, the far end occluded), so they are weighted less in the score.
// Sources: manufacturer dimensions of the common local models — Toyota
// Axio/Premio/Corolla (sedan), RAV4/Prado/X-Trail (SUV), Hilux/D-Max
// (pickup), Hiace/NV350 (van), Isuzu NQR/FRR (truck).
// Priors reflect a residential estate's traffic (mostly saloons and
// SUVs) and only matter when the dimensions are ambiguous.
constexpr ClassEnvelope kEnvelopes[] = {
    {VehicleClass::kPedestrian, 0.2f, 1.0f, 0.2f, 1.0f, 1.0f, 2.1f, 0.8f},
    {VehicleClass::kBicycle, 1.4f, 2.0f, 0.3f, 0.8f, 0.8f, 1.9f, 0.5f},
    {VehicleClass::kMotorcycle, 1.6f, 2.5f, 0.6f, 1.1f, 1.0f, 1.7f, 0.6f},
    {VehicleClass::kSedan, 3.6f, 5.1f, 1.6f, 1.95f, 1.30f, 1.55f, 1.0f},
    {VehicleClass::kSuv, 4.2f, 5.0f, 1.75f, 2.05f, 1.58f, 1.95f, 1.0f},
    {VehicleClass::kPickup, 5.0f, 5.6f, 1.75f, 2.05f, 1.60f, 1.95f, 0.85f},
    {VehicleClass::kVan, 4.4f, 5.6f, 1.65f, 2.10f, 1.96f, 2.50f, 0.7f},
    {VehicleClass::kTruck, 5.8f, 12.0f, 2.0f, 2.6f, 2.4f, 4.2f, 0.5f},
};

float wrap_half_pi(float a) {
    // Headings are axes, not directions: fold into (-pi/2, pi/2].
    constexpr float kPi = std::numbers::pi_v<float>;
    while (a > kPi / 2) {
        a -= kPi;
    }
    while (a <= -kPi / 2) {
        a += kPi;
    }
    return a;
}

// Solve the 3x3 system M·s = r (Cramer's rule); false if singular.
bool solve3(const double m[9], const double r[3], double s[3]) {
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                       m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-9) {
        return false;
    }
    for (int c = 0; c < 3; ++c) {
        double t[9];
        std::copy(m, m + 9, t);
        for (int row = 0; row < 3; ++row) {
            t[row * 3 + c] = r[row];
        }
        s[c] = (t[0] * (t[4] * t[8] - t[5] * t[7]) - t[1] * (t[3] * t[8] - t[5] * t[6]) +
                t[2] * (t[3] * t[7] - t[4] * t[6])) /
               det;
    }
    return true;
}

}  // namespace

ClassScores classify_dimensions(float length, float width, float height) {
    ClassScores out;
    float total = 0.0f;
    for (const auto& e : kEnvelopes) {
        // Height is two-sided (the roof is seen); width and length are
        // lower bounds (the far side / far end are often occluded).
        const float mh = envelope(height, e.h_lo, e.h_hi, 0.10f);
        const float mw = lower_bound_envelope(width, e.w_lo, e.w_hi, 0.20f, 0.5f);
        const float ml = lower_bound_envelope(length, e.l_lo, e.l_hi, 0.45f, 0.35f);
        const float s = e.prior * mh * std::pow(mw, 0.5f) * std::pow(ml, 0.7f);
        out.p[static_cast<std::size_t>(e.cls)] = s;
        total += s;
        if (s > out.raw) {
            out.raw = s;
            out.best = e.cls;
        }
    }
    if (total <= 1e-6f) {
        out.best = VehicleClass::kUnknown;
        out.best_p = 0.0f;
        return out;
    }
    for (auto& v : out.p) {
        v /= total;
    }
    out.best_p = out.p[static_cast<std::size_t>(out.best)];
    return out;
}

VehicleDetector::VehicleDetector(DetectorConfig cfg) : cfg_(std::move(cfg)), tf_(cfg_.mount) {}

std::vector<VehicleObservation> VehicleDetector::update(const PointCloud& batch) {
    const auto t0 = std::chrono::steady_clock::now();

    // 1. Sensor → gate frame, range gate, crop, mask.
    Stamped s;
    s.stamp = batch.stamp;
    s.raw = batch.points.size();
    s.pts.reserve(batch.points.size());
    const float r2_min = cfg_.range_min * cfg_.range_min;
    const float r2_max = cfg_.range_max * cfg_.range_max;
    for (const Point& p : batch.points) {
        const float r2 = p.x * p.x + p.y * p.y + p.z * p.z;
        if (!(r2 >= r2_min && r2 <= r2_max)) {  // also drops NaN
            continue;
        }
        const Vec3 g = tf_.apply(p.x, p.y, p.z);
        if (!cfg_.track_zone.contains(g.x, g.y, g.z)) {
            continue;
        }
        bool masked = false;
        for (const Zone& z : cfg_.exclude_zones) {
            if (z.contains(g.x, g.y, g.z)) {
                masked = true;
                break;
            }
        }
        if (!masked) {
            s.pts.push_back({g.x, g.y, g.z, p.intensity});
        }
    }

    // 2. Sliding window. A clock step backwards (replay restart) resets it.
    if (!window_.empty() && window_.back().stamp > batch.stamp) {
        window_.clear();
    }
    window_.push_back(std::move(s));
    while (!window_.empty() && window_.front().stamp < batch.stamp - cfg_.window) {
        window_.pop_front();
    }
    scratch_.clear();
    window_raw_ = 0;
    for (const auto& w : window_) {
        scratch_.insert(scratch_.end(), w.pts.begin(), w.pts.end());
        window_raw_ += w.raw;
    }

    // 3–7.
    fit_ground();
    cluster();
    track(batch.stamp);
    report(batch.stamp);

    stats_.window_points = scratch_.size();
    stats_.clusters = clusters_.size();
    stats_.tracks = tracks_.size();
    stats_.ground_a = ga_;
    stats_.ground_b = gb_;
    stats_.ground_c = gc_;
    stats_.update_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return report_;
}

void VehicleDetector::fit_ground() {
    if (cfg_.fit_ground_plane) {
        std::vector<std::size_t> cand;
        cand.reserve(scratch_.size() / 2);
        for (std::size_t i = 0; i < scratch_.size(); ++i) {
            if (scratch_[i].z < cfg_.ground_search_max) {
                cand.push_back(i);
            }
        }
        if (cand.size() >= 30) {
            // Subsample to bound the RANSAC cost.
            const std::size_t stride = std::max<std::size_t>(1, cand.size() / 1500);
            std::vector<std::size_t> sample;
            for (std::size_t i = 0; i < cand.size(); i += stride) {
                sample.push_back(cand[i]);
            }
            auto next_rand = [this](std::size_t n) {
                rng_ = rng_ * 1664525u + 1013904223u;  // deterministic LCG
                return static_cast<std::size_t>(rng_ >> 8) % n;
            };
            float best_a = 0, best_b = 0, best_c = 0;
            std::size_t best_inliers = 0;
            constexpr float kInlier = 0.06f;
            constexpr float kMaxSlope = 0.15f;  // ~8.5°: a driveway, not a wall
            for (int it = 0; it < 50; ++it) {
                const Point& p1 = scratch_[sample[next_rand(sample.size())]];
                const Point& p2 = scratch_[sample[next_rand(sample.size())]];
                const Point& p3 = scratch_[sample[next_rand(sample.size())]];
                const float ux = p2.x - p1.x, uy = p2.y - p1.y, uz = p2.z - p1.z;
                const float vx = p3.x - p1.x, vy = p3.y - p1.y, vz = p3.z - p1.z;
                const float nx = uy * vz - uz * vy;
                const float ny = uz * vx - ux * vz;
                const float nz = ux * vy - uy * vx;
                if (std::abs(nz) < 1e-4f) {
                    continue;
                }
                const float a = -nx / nz, b = -ny / nz;
                if (std::abs(a) > kMaxSlope || std::abs(b) > kMaxSlope) {
                    continue;
                }
                const float c = p1.z - a * p1.x - b * p1.y;
                std::size_t inliers = 0;
                for (const std::size_t i : sample) {
                    const Point& q = scratch_[i];
                    if (std::abs(q.z - (a * q.x + b * q.y + c)) < kInlier) {
                        ++inliers;
                    }
                }
                if (inliers > best_inliers) {
                    best_inliers = inliers;
                    best_a = a;
                    best_b = b;
                    best_c = c;
                }
            }
            if (best_inliers >= 20) {
                // Least-squares refine on all candidate inliers.
                double m[9] = {}, r[3] = {};
                std::size_t n = 0;
                for (const std::size_t i : cand) {
                    const Point& q = scratch_[i];
                    if (std::abs(q.z - (best_a * q.x + best_b * q.y + best_c)) >= kInlier) {
                        continue;
                    }
                    const double x = q.x, y = q.y, z = q.z;
                    m[0] += x * x, m[1] += x * y, m[2] += x;
                    m[4] += y * y, m[5] += y;
                    r[0] += x * z, r[1] += y * z, r[2] += z;
                    ++n;
                }
                m[3] = m[1], m[6] = m[2], m[7] = m[5], m[8] = static_cast<double>(n);
                double sol[3];
                if (n >= 20 && solve3(m, r, sol) && std::abs(sol[0]) <= kMaxSlope &&
                    std::abs(sol[1]) <= kMaxSlope) {
                    best_a = static_cast<float>(sol[0]);
                    best_b = static_cast<float>(sol[1]);
                    best_c = static_cast<float>(sol[2]);
                }
                if (!ground_init_) {
                    ga_ = best_a, gb_ = best_b, gc_ = best_c;
                    ground_init_ = true;
                } else {
                    constexpr float kAlpha = 0.3f;  // smooth tick-to-tick jitter
                    ga_ += kAlpha * (best_a - ga_);
                    gb_ += kAlpha * (best_b - gb_);
                    gc_ += kAlpha * (best_c - gc_);
                }
            }
        }
    }
    height_.resize(scratch_.size());
    for (std::size_t i = 0; i < scratch_.size(); ++i) {
        const Point& p = scratch_[i];
        height_[i] = p.z - (ga_ * p.x + gb_ * p.y + gc_);
    }
}

void VehicleDetector::cluster() {
    clusters_.clear();
    const Zone& z = cfg_.track_zone;
    const float cell = cfg_.bev_cell;
    const int nx = std::max(1, static_cast<int>(std::ceil((z.x_max - z.x_min) / cell)));
    const int ny = std::max(1, static_cast<int>(std::ceil((z.y_max - z.y_min) / cell)));
    const std::size_t ncells = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);

    // Counting sort of object points into cells.
    std::vector<std::uint32_t> start(ncells + 1, 0);
    std::vector<std::int32_t> cell_of(scratch_.size(), -1);
    std::size_t object_points = 0;
    for (std::size_t i = 0; i < scratch_.size(); ++i) {
        if (height_[i] <= cfg_.ground_clearance) {
            continue;
        }
        const int cx = std::clamp(static_cast<int>((scratch_[i].x - z.x_min) / cell), 0, nx - 1);
        const int cy = std::clamp(static_cast<int>((scratch_[i].y - z.y_min) / cell), 0, ny - 1);
        const auto c = static_cast<std::int32_t>(cy * nx + cx);
        cell_of[i] = c;
        ++start[static_cast<std::size_t>(c) + 1];
        ++object_points;
    }
    stats_.object_points = object_points;
    for (std::size_t c = 0; c < ncells; ++c) {
        start[c + 1] += start[c];
    }
    std::vector<std::uint32_t> order(object_points);
    {
        std::vector<std::uint32_t> fill(start.begin(), start.end() - 1);
        for (std::size_t i = 0; i < scratch_.size(); ++i) {
            if (cell_of[i] >= 0) {
                order[fill[static_cast<std::size_t>(cell_of[i])]++] = static_cast<std::uint32_t>(i);
            }
        }
    }

    // Connected components over occupied cells. The bridged gap at a
    // cell follows the expected point spacing at its range: the window's
    // raw points spread over the sensor's hemisphere (2π sr).
    const float angular_spacing =
        std::sqrt(2.0f * std::numbers::pi_v<float> /
                  static_cast<float>(std::max<std::size_t>(window_raw_, 1)));
    const Vec3 sensor = tf_.translation();
    auto bridge_at = [&](int cx, int cy) {
        const float x = z.x_min + (static_cast<float>(cx) + 0.5f) * cell;
        const float y = z.y_min + (static_cast<float>(cy) + 0.5f) * cell;
        const float range = std::hypot(x - sensor.x, y - sensor.y, sensor.z);
        const float gap =
            std::max(cfg_.cluster_gap_min, cfg_.cluster_gap_scale * range * angular_spacing);
        // Cells k apart hold points at least (k-1)·cell apart, so k =
        // floor(gap/cell) bridges gaps up to about `gap` and no further.
        return std::clamp(static_cast<int>(gap / cell), 1, std::max(1, cfg_.bridge_cells_max));
    };
    std::vector<std::int32_t> label(ncells, -1);
    std::vector<std::int32_t> queue;
    std::vector<std::uint32_t> members;
    int next_label = 0;
    for (std::size_t seed = 0; seed < ncells; ++seed) {
        if (start[seed + 1] == start[seed] || label[seed] >= 0) {
            continue;
        }
        label[seed] = next_label;
        queue.assign(1, static_cast<std::int32_t>(seed));
        members.clear();
        for (std::size_t q = 0; q < queue.size(); ++q) {
            const auto c = static_cast<std::size_t>(queue[q]);
            members.insert(members.end(), order.begin() + start[c], order.begin() + start[c + 1]);
            const int cx = static_cast<int>(c) % nx;
            const int cy = static_cast<int>(c) / nx;
            const int k = bridge_at(cx, cy);
            for (int dy = -k; dy <= k; ++dy) {
                const int y = cy + dy;
                if (y < 0 || y >= ny) {
                    continue;
                }
                for (int dx = -k; dx <= k; ++dx) {
                    const int x = cx + dx;
                    if (x < 0 || x >= nx) {
                        continue;
                    }
                    const auto n = static_cast<std::size_t>(y * nx + x);
                    if (label[n] < 0 && start[n + 1] != start[n]) {
                        label[n] = next_label;
                        queue.push_back(static_cast<std::int32_t>(n));
                    }
                }
            }
        }
        ++next_label;

        // 5. Box fit for this component.
        if (members.size() < cfg_.min_cluster_points) {
            continue;
        }
        std::vector<float> hs;
        hs.reserve(members.size());
        double mx = 0, my = 0;
        for (const auto i : members) {
            hs.push_back(height_[i]);
            mx += scratch_[i].x;
            my += scratch_[i].y;
        }
        const double n = static_cast<double>(members.size());
        mx /= n;
        my /= n;
        // Robust top: the 98th percentile once there are enough points
        // (one stray return must not make a sedan a van).
        std::sort(hs.begin(), hs.end());
        const float height =
            hs.size() >= 50
                ? hs[static_cast<std::size_t>(0.98 * static_cast<double>(hs.size() - 1))]
                : hs.back();
        if (height < cfg_.min_cluster_height) {
            continue;
        }
        double sxx = 0, syy = 0, sxy = 0;
        for (const auto i : members) {
            const double dx = scratch_[i].x - mx, dy = scratch_[i].y - my;
            sxx += dx * dx;
            syy += dy * dy;
            sxy += dx * dy;
        }
        const double half_diff = (sxx - syy) / 2.0;
        const double root = std::sqrt(half_diff * half_diff + sxy * sxy);
        const double l1 = (sxx + syy) / 2.0 + root;
        const double l2 = (sxx + syy) / 2.0 - root;
        float yaw = 0.0f;
        // Off the lane-aligned default, a clearly elongated footprint
        // gives its own heading; a blob (head-on, sparse) takes the lane axis.
        if (!cfg_.lane_aligned && (l2 <= 1e-9 || l1 / l2 > 2.0)) {
            yaw = wrap_half_pi(static_cast<float>(0.5 * std::atan2(2.0 * sxy, sxx - syy)));
        }
        const float c = std::cos(yaw), sn = std::sin(yaw);
        float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
        float near_x = 1e9f;
        for (const auto i : members) {
            near_x = std::min(near_x, scratch_[i].x);
            const float dx = scratch_[i].x - static_cast<float>(mx);
            const float dy = scratch_[i].y - static_cast<float>(my);
            const float u = c * dx + sn * dy;
            const float v = -sn * dx + c * dy;
            umin = std::min(umin, u), umax = std::max(umax, u);
            vmin = std::min(vmin, v), vmax = std::max(vmax, v);
        }
        Cluster cl;
        const float um = (umin + umax) / 2, vm = (vmin + vmax) / 2;
        cl.cx = static_cast<float>(mx) + c * um - sn * vm;
        cl.cy = static_cast<float>(my) + sn * um + c * vm;
        cl.length = umax - umin;
        cl.width = vmax - vmin;
        cl.height = height;
        cl.yaw = yaw;
        cl.near_x = near_x;
        cl.cz = ga_ * cl.cx + gb_ * cl.cy + gc_ + height / 2.0f;
        cl.points = static_cast<std::uint32_t>(members.size());
        clusters_.push_back(cl);
    }
}

void VehicleDetector::track(Clock::time_point now) {
    for (auto& t : tracks_) {
        t.updated = false;
    }
    struct Pair {
        float d;
        std::size_t t, c;
    };
    std::vector<Pair> pairs;
    for (std::size_t ti = 0; ti < tracks_.size(); ++ti) {
        const Track& t = tracks_[ti];
        const float dt = std::chrono::duration<float>(now - t.last_seen).count();
        const float px = t.x + t.vx * dt, py = t.y + t.vy * dt;
        const float speed = std::hypot(t.vx, t.vy);
        const float gate = cfg_.association_gate + speed * dt;
        for (std::size_t ci = 0; ci < clusters_.size(); ++ci) {
            const float d = std::hypot(clusters_[ci].near_x - px, clusters_[ci].cy - py);
            if (d < gate) {
                pairs.push_back({d, ti, ci});
            }
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.d < b.d; });
    std::vector<bool> cluster_used(clusters_.size(), false);
    std::vector<bool> track_used(tracks_.size(), false);
    const float window_s = std::chrono::duration<float>(cfg_.window).count();

    for (const Pair& pr : pairs) {
        if (track_used[pr.t] || cluster_used[pr.c]) {
            continue;
        }
        track_used[pr.t] = cluster_used[pr.c] = true;
        Track& t = tracks_[pr.t];
        const Cluster& cl = clusters_[pr.c];
        const float dt = std::max(1e-3f, std::chrono::duration<float>(now - t.last_seen).count());
        // Alpha-beta filter on the footprint centre.
        const float px = t.x + t.vx * dt, py = t.y + t.vy * dt;
        const float rx = cl.near_x - px, ry = cl.cy - py;
        constexpr float kAlpha = 0.6f, kBeta = 0.25f, kMaxSpeed = 15.0f;
        t.x = px + kAlpha * rx;
        t.y = py + kAlpha * ry;
        t.vx += kBeta / dt * rx;
        t.vy += kBeta / dt * ry;
        const float speed = std::hypot(t.vx, t.vy);
        if (speed > kMaxSpeed) {
            t.vx *= kMaxSpeed / speed;
            t.vy *= kMaxSpeed / speed;
        }
        // Heading: smooth as an axis (period pi).
        const float dyaw = wrap_half_pi(cl.yaw - t.yaw);
        t.yaw = wrap_half_pi(t.yaw + 0.3f * dyaw);
        // A moving vehicle is smeared along its heading by the window.
        const float along = std::abs(t.vx * std::cos(t.yaw) + t.vy * std::sin(t.yaw));
        const float length = std::max(cl.length * 0.6f, cl.length - along * window_s);
        // Grow fast, shrink slowly: a sparse or partly occluded tick
        // never shrinks the vehicle much.
        auto blend = [](float est, float obs) {
            return est + (obs > est ? 0.5f : 0.08f) * (obs - est);
        };
        t.length = blend(t.length, length);
        t.width = blend(t.width, cl.width);
        t.height = blend(t.height, cl.height);
        t.cz += 0.3f * (cl.cz - t.cz);
        t.points = cl.points;
        ++t.hits;
        t.last_seen = now;
        t.updated = true;
    }

    for (std::size_t ci = 0; ci < clusters_.size(); ++ci) {
        if (cluster_used[ci]) {
            continue;
        }
        const Cluster& cl = clusters_[ci];
        Track t;
        t.id = next_track_id_++;
        if (next_track_id_ == 0) {
            next_track_id_ = 1;  // 0 means "untracked" on the wire
        }
        t.x = cl.near_x, t.y = cl.cy, t.cz = cl.cz;
        t.length = cl.length, t.width = cl.width, t.height = cl.height, t.yaw = cl.yaw;
        t.points = cl.points;
        t.hits = 1;
        t.last_seen = now;
        t.updated = true;
        tracks_.push_back(t);
    }

    std::erase_if(tracks_, [&](const Track& t) { return now - t.last_seen > cfg_.track_timeout; });
}

void VehicleDetector::report(Clock::time_point now) {
    report_.clear();
    for (const Track& t : tracks_) {
        if (now - t.last_seen > cfg_.coast || t.hits < cfg_.confirm_hits ||
            !cfg_.trigger_zone.contains_xy(t.x, t.y)) {
            continue;
        }
        const ClassScores sc = classify_dimensions(t.length, t.width, t.height);
        // Evidence: a track seen for a few ticks with a fair number of
        // points earns full weight; a fresh, sparse one does not.
        const float evidence = std::min(1.0f, static_cast<float>(t.hits) / 3.0f) *
                               std::min(1.0f, static_cast<float>(t.points) / 40.0f);
        VehicleObservation v;
        if (sc.raw < 0.05f) {
            // Something is there, but it fits no class envelope.
            v.vehicle_class = VehicleClass::kUnknown;
            v.confidence = 0.3f * evidence;
        } else {
            v.vehicle_class = sc.best;
            v.confidence = sc.best_p * std::sqrt(std::min(1.0f, sc.raw / 0.5f)) * evidence;
        }
        // The box runs from the tracked near face across the tracked length
        // (lane-aligned: along +X).
        v.box.center_x = t.x + std::cos(t.yaw) * t.length / 2.0f;
        v.box.center_y = t.y + std::sin(t.yaw) * t.length / 2.0f;
        v.box.center_z = t.cz;
        v.box.length = t.length;
        v.box.width = t.width;
        v.box.height = t.height;
        v.box.yaw_radians = t.yaw;
        v.velocity_x = t.vx;
        v.velocity_y = t.vy;
        v.track_id = t.id;
        v.point_count = t.points;
        report_.push_back(v);
    }
    std::sort(report_.begin(), report_.end(), [](const auto& a, const auto& b) {
        return std::hypot(a.box.center_x, a.box.center_y) <
               std::hypot(b.box.center_x, b.box.center_y);
    });
}

}  // namespace gate::vision::lidar
