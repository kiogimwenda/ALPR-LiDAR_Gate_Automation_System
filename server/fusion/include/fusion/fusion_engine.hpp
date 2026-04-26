// fusion_engine.hpp — ALPR + LiDAR decision fusion.
//
// Given an AuthorizeRequest (DetectionFrame + site context), produce a final
// AuthDecision. The verdict ladder follows docs/diagrams/04-fusion-decision.mmd:
//
//     no plate                       → DENIED       (NO_PLATE_FOUND)
//     gate force-closed              → DENIED       (FORCE_CLOSE)
//     combined conf < threshold      → LOW_CONFIDENCE
//     plate is blocklisted           → DENIED       (BLOCKLISTED)
//     gate force-open                → AUTHORIZED   (override)
//     guard override_allowlist=true  → AUTHORIZED   (manual approval)
//     plate not on allowlist         → DENIED       (NOT_ON_ALLOWLIST)
//     allowlist hit but class wrong  → MANUAL_REVIEW(CLASS_MISMATCH)
//     allowlist hit but outside time → DENIED       (OUTSIDE_WINDOW)
//     all gates pass                 → AUTHORIZED
//
// The engine holds a mutable map of per-gate override states (force-open /
// force-close / none) — these are set by the dashboard via gRPC and consumed
// here. Allowlist/blocklist storage lives in gate::auth::AllowlistStore;
// the engine borrows a non-owning reference and never closes the database.

#pragma once

#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>

#include "auth/allowlist_store.hpp"
#include "gate_service.pb.h"

namespace gate::fusion {

struct FusionConfig {
    // Linear blend of ALPR and LiDAR confidences.
    float alpr_weight = 0.5f;
    float lidar_weight = 0.5f;

    // Combined-confidence floor below which the verdict is LOW_CONFIDENCE.
    float min_combined_threshold = 0.7f;

    // What confidence to substitute when the frame contains no LiDAR
    // detections. Default 0 — an ALPR-only frame falls below the
    // threshold unless `lidar_weight` is also 0.
    float lidar_missing_confidence = 0.0f;
};

enum class OverrideState : std::uint8_t {
    kNone,        // Normal operation.
    kForceOpen,   // Latched-open by guard — every request authorizes.
    kForceClose,  // Lockdown — every request denies.
};

class FusionEngine {
public:
    // Borrows the store; the caller is responsible for keeping it alive.
    explicit FusionEngine(gate::auth::AllowlistStore& store, FusionConfig cfg = {});

    // Override management — typically called from the dashboard service when
    // a guard issues LATCH_OPEN / LATCH_CLOSE / RELEASE_LATCH.
    void set_override(const std::string& gate_id, OverrideState state);
    OverrideState override_for(const std::string& gate_id) const;

    // Production decision path — uses system time.
    gate::v1::AuthDecision decide(const gate::v1::AuthorizeRequest& req) const;

    // Test/deterministic path — caller supplies the wall-clock instant and
    // the matching local-time breakdown (for time-window evaluation).
    gate::v1::AuthDecision decide_at(const gate::v1::AuthorizeRequest& req, std::time_t now_unix,
                                     const std::tm& now_local) const;

private:
    gate::auth::AllowlistStore& store_;
    FusionConfig cfg_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, OverrideState> overrides_;
};

// RFC 4122 UUIDv4 string. Uses thread-local mt19937 seeded once from
// std::random_device — fine for decision IDs (collision space is 122 bits).
std::string generate_uuidv4();

}  // namespace gate::fusion
