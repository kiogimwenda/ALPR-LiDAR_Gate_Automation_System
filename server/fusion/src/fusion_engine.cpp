// fusion_engine.cpp

#include "fusion/fusion_engine.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <random>

namespace gate::fusion {

using gate::v1::AuthDecision;
using gate::v1::AuthorizeRequest;
using gate::v1::AuthVerdict;
using gate::v1::DenyReason;
using gate::v1::PlateDetection;
using gate::v1::VehicleClass;
using gate::v1::VehicleDetection;

namespace {

// Combine YOLO detection + PaddleOCR mean-character confidences into a
// single ALPR side score. Mean keeps the math symmetric — a weak side
// pulls the result down — and is the default the docs/diagrams call out.
float fold_alpr_confidence(const PlateDetection& p) {
    const float det = std::max(p.detection_conf(), 0.0f);
    const float ocr = std::max(p.ocr_conf(), 0.0f);
    return 0.5f * det + 0.5f * ocr;
}

// Pick the single most-confident detection from a frame. Returns -1 when
// the list is empty so the caller can branch on "no candidate."
int pick_best_plate(const AuthorizeRequest& req) {
    int best = -1;
    float best_score = -1.0f;
    for (int i = 0; i < req.frame().plates_size(); ++i) {
        const float s = req.frame().plates(i).detection_conf();
        if (s > best_score) {
            best = i;
            best_score = s;
        }
    }
    return best;
}

int pick_best_vehicle(const AuthorizeRequest& req) {
    int best = -1;
    float best_score = -1.0f;
    for (int i = 0; i < req.frame().vehicles_size(); ++i) {
        const float s = req.frame().vehicles(i).class_conf();
        if (s > best_score) {
            best = i;
            best_score = s;
        }
    }
    return best;
}

bool class_in_allowlist(const gate::v1::Allowlistentry& e, VehicleClass cls) {
    if (e.allowed_classes_size() == 0)
        return true;
    for (int i = 0; i < e.allowed_classes_size(); ++i) {
        if (e.allowed_classes(i) == cls)
            return true;
    }
    return false;
}

}  // namespace

FusionEngine::FusionEngine(gate::auth::AllowlistStore& store, FusionConfig cfg)
    : store_(store), cfg_(cfg) {}

void FusionEngine::set_override(const std::string& gate_id, OverrideState state) {
    std::lock_guard lk{mu_};
    if (state == OverrideState::kNone) {
        overrides_.erase(gate_id);
    } else {
        overrides_[gate_id] = state;
    }
}

OverrideState FusionEngine::override_for(const std::string& gate_id) const {
    std::lock_guard lk{mu_};
    const auto it = overrides_.find(gate_id);
    return it == overrides_.end() ? OverrideState::kNone : it->second;
}

AuthDecision FusionEngine::decide(const AuthorizeRequest& req) const {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    return decide_at(req, now, local);
}

AuthDecision FusionEngine::decide_at(const AuthorizeRequest& req, std::time_t now_unix,
                                     const std::tm& now_local) const {
    AuthDecision out;
    out.set_decision_id(generate_uuidv4());
    out.mutable_decision_ts()->set_seconds(now_unix);
    out.set_gate_id(req.frame().gate_id());
    out.set_frame_id(req.frame().frame_id());
    out.set_actor(req.actor());

    // 1. Force-close override wins over everything — even a guard's manual
    //    approval cannot defeat a lockdown.
    const auto override_state = override_for(req.frame().gate_id());
    if (override_state == OverrideState::kForceClose) {
        out.set_verdict(AuthVerdict::AUTH_VERDICT_DENIED);
        out.set_deny_reason(DenyReason::DENY_REASON_FORCE_CLOSE);
        out.set_reason_text("force-close lockdown active");
        return out;
    }

    // 2. No plate detected at all — record the verdict and return early. A
    //    follow-up ALPR retry might find one; the dashboard surfaces this
    //    as "frame had no recognizable plate."
    const int best_plate_idx = pick_best_plate(req);
    if (best_plate_idx < 0) {
        out.set_verdict(AuthVerdict::AUTH_VERDICT_DENIED);
        out.set_deny_reason(DenyReason::DENY_REASON_NO_PLATE_FOUND);
        out.set_reason_text("no plate detected in frame");
        return out;
    }
    const auto& plate = req.frame().plates(best_plate_idx);
    const std::string normalized = gate::auth::normalize_plate(plate.plate_text());
    out.set_matched_plate(normalized);

    // 3. LiDAR side — pick the most confident vehicle, or fall back to the
    //    configured "missing" confidence (default 0) when no vehicle was
    //    classified. matched_class is recorded either way.
    float lidar_conf = cfg_.lidar_missing_confidence;
    VehicleClass vehicle_class = VehicleClass::VEHICLE_CLASS_UNKNOWN;
    if (const int v = pick_best_vehicle(req); v >= 0) {
        const auto& vd = req.frame().vehicles(v);
        lidar_conf = vd.class_conf();
        vehicle_class = vd.class_();
    }
    out.set_matched_class(vehicle_class);

    // 4. Combined confidence is the linear blend the diagram specifies.
    const float alpr_conf = fold_alpr_confidence(plate);
    const float combined = cfg_.alpr_weight * alpr_conf + cfg_.lidar_weight * lidar_conf;
    out.set_combined_conf(combined);

    // 5. Force-open override still records the matched plate but skips
    //    every gate below — the guard already decided.
    if (override_state == OverrideState::kForceOpen) {
        out.set_verdict(AuthVerdict::AUTH_VERDICT_AUTHORIZED);
        out.set_reason_text("force-open override active");
        return out;
    }

    // 6. Confidence floor. Pick the more informative sub-reason: whichever
    //    side dragged the combined score down. Ties fall to ALPR (the more
    //    typical failure mode at gates).
    if (combined < cfg_.min_combined_threshold) {
        out.set_verdict(AuthVerdict::AUTH_VERDICT_LOW_CONFIDENCE);
        out.set_deny_reason(alpr_conf <= lidar_conf ? DenyReason::DENY_REASON_LOW_ALPR_CONF
                                                    : DenyReason::DENY_REASON_LOW_LIDAR_CONF);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "combined confidence %.3f below threshold %.3f "
                      "(alpr=%.3f, lidar=%.3f)",
                      static_cast<double>(combined),
                      static_cast<double>(cfg_.min_combined_threshold),
                      static_cast<double>(alpr_conf), static_cast<double>(lidar_conf));
        out.set_reason_text(buf);
        return out;
    }

    // 7. Blocklist always wins over allowlist — even guard override cannot
    //    let a blocklisted plate through. Lockdown semantics.
    if (store_.is_blocklisted(req.site_id(), normalized)) {
        out.set_verdict(AuthVerdict::AUTH_VERDICT_DENIED);
        out.set_deny_reason(DenyReason::DENY_REASON_BLOCKLISTED);
        out.set_reason_text("plate is blocklisted");
        return out;
    }

    // 8. Guard manual override — bypasses allowlist (but not blocklist).
    if (req.override_allowlist()) {
        out.set_verdict(AuthVerdict::AUTH_VERDICT_AUTHORIZED);
        out.set_reason_text("guard manual override (allowlist bypassed)");
        return out;
    }

    // 9. Allowlist lookup. Site-scoped; no entry → DENIED.
    const auto entry = store_.lookup(req.site_id(), normalized);
    if (!entry) {
        out.set_verdict(AuthVerdict::AUTH_VERDICT_DENIED);
        out.set_deny_reason(DenyReason::DENY_REASON_NOT_ON_ALLOWLIST);
        out.set_reason_text("plate not on allowlist");
        return out;
    }

    // 10. Time-window + class restriction. Disambiguate the failure: a
    //     class mismatch surfaces as MANUAL_REVIEW (a guard might still
    //     authorize) while an outside-window denial is a hard deny.
    if (!gate::auth::is_allowed_now(*entry, vehicle_class, now_unix, now_local)) {
        if (!class_in_allowlist(*entry, vehicle_class)) {
            out.set_verdict(AuthVerdict::AUTH_VERDICT_MANUAL_REVIEW);
            out.set_deny_reason(DenyReason::DENY_REASON_CLASS_MISMATCH);
            out.set_reason_text("plate match but vehicle class not permitted");
        } else {
            out.set_verdict(AuthVerdict::AUTH_VERDICT_DENIED);
            out.set_deny_reason(DenyReason::DENY_REASON_OUTSIDE_WINDOW);
            out.set_reason_text("plate match but outside allowed time window");
        }
        return out;
    }

    out.set_verdict(AuthVerdict::AUTH_VERDICT_AUTHORIZED);
    out.set_reason_text("plate + class match");
    return out;
}

std::string generate_uuidv4() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<unsigned> dist(0, 255);
    std::array<unsigned char, 16> b{};
    for (auto& x : b)
        x = static_cast<unsigned char>(dist(rng));
    // Version 4: high nibble of byte 6 is 0x4.
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);
    // Variant 1 (RFC 4122): top two bits of byte 8 are 10.
    b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);
    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                  "%02x%02x%02x%02x%02x%02x",
                  b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12],
                  b[13], b[14], b[15]);
    return std::string{buf};
}

}  // namespace gate::fusion
