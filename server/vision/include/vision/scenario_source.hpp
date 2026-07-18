// scenario_source.hpp — deterministic scripted ObservationSource (Phase 5.2).
//
// Replays a JSON scenario file as a paced stream of ObservationSets so
// the full submit → fusion → decision path can be exercised without a
// camera or LiDAR. Pacing goes through an injectable Clock so unit
// tests replay a scenario instantly with a fake clock; production uses
// the steady-clock default.
//
// Scenario JSON schema (parsing is FAIL-CLOSED — any violation throws
// std::runtime_error with the offending path; nothing is skipped):
//
//   {
//     "loop": 1,                       // optional int >= 1, default 1
//     "events": [                      // required, non-empty
//       {
//         "offset_ms": 0,              // required int >= 0, from start
//                                      // of the current loop iteration
//         "plates": [                  // optional, default []
//           {
//             "text": "KDA123X",       // required non-empty string
//             "detection_conf": 0.94,  // required float in [0, 1]
//             "ocr_conf": 0.91,        // required float in [0, 1]
//             "box": {"x": 0, "y": 0, "width": 0, "height": 0},  // optional
//             "country_hint": "KE",    // optional
//             "raw_ocr": "KDA 123X"    // optional
//           }
//         ],
//         "vehicles": [                // optional, default []
//           {
//             "class": "sedan",        // required: unknown|pedestrian|
//                                      // bicycle|motorcycle|sedan|suv|
//                                      // pickup|van|truck
//             "confidence": 0.88,      // required float in [0, 1]
//             "box": {"center_x": 0.0, "center_y": 0.0, "center_z": 0.0,
//                     "length": 0.0, "width": 0.0, "height": 0.0,
//                     "yaw_radians": 0.0},  // optional
//             "track_id": 7,           // optional uint
//             "point_count": 412       // optional uint
//           }
//         ]
//       }
//     ]
//   }
//
// Loop semantics: the whole event list replays `loop` times; each
// iteration re-bases offsets at the moment the iteration begins, so a
// looped scenario never drifts ahead of the clock.

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "vision/source.hpp"

namespace gate::vision {

// Pacing seam. Production code uses SteadyClock; tests inject a fake
// that advances instantly, so no unit test ever really sleeps.
class Clock {
public:
    virtual ~Clock() = default;

    // Monotonic instant used for event pacing.
    virtual std::chrono::steady_clock::time_point now() = 0;

    // Wall-clock instant stamped onto emitted ObservationSets (and from
    // there onto DetectionFrame.capture_ts).
    virtual std::chrono::system_clock::time_point wall_now() = 0;

    // Blocks until `tp`; must be a no-op when `tp` is already past.
    virtual void sleep_until(std::chrono::steady_clock::time_point tp) = 0;
};

// Default Clock backed by the real std::chrono clocks.
class SteadyClock final : public Clock {
public:
    std::chrono::steady_clock::time_point now() override;
    std::chrono::system_clock::time_point wall_now() override;
    void sleep_until(std::chrono::steady_clock::time_point tp) override;
};

// One parsed scenario event: emit these observations `offset` after the
// start of the current loop iteration.
struct ScenarioEvent {
    std::chrono::milliseconds offset{0};
    std::vector<PlateObservation> plates;
    std::vector<VehicleObservation> vehicles;
};

class ScenarioSource final : public ObservationSource {
public:
    // Parse from a JSON string. Throws std::runtime_error on malformed
    // JSON or any schema violation (fail-closed).
    static ScenarioSource from_json(const std::string& json_text,
                                    std::shared_ptr<Clock> clock = nullptr);

    // Parse from a file. Unreadable path throws — a scenario that
    // silently played nothing would mask a misconfigured deployment.
    static ScenarioSource from_file(const std::filesystem::path& path,
                                    std::shared_ptr<Clock> clock = nullptr);

    // Sleeps until the next event is due, then emits it. nullopt once
    // every loop iteration has been replayed.
    std::optional<ObservationSet> next() override;

    std::size_t event_count() const { return events_.size(); }
    unsigned loop_count() const { return loops_; }

private:
    ScenarioSource(std::vector<ScenarioEvent> events, unsigned loops, std::shared_ptr<Clock> clock);

    std::vector<ScenarioEvent> events_;
    unsigned loops_ = 1;
    std::shared_ptr<Clock> clock_;

    std::size_t index_ = 0;   // Next event within the iteration
    unsigned iteration_ = 0;  // Completed loop iterations
    bool epoch_set_ = false;  // Epoch is captured lazily per iteration
    std::chrono::steady_clock::time_point epoch_{};
};

}  // namespace gate::vision
