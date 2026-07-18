// frame_builder.hpp — ObservationSet → gate::v1::DetectionFrame (Phase 5.2).
//
// The single place plain observations become wire protobuf. One builder
// per gate: frame_id is monotonic per gate_id (the proto's contract),
// starting at 1 so frame_id 0 unambiguously means "never assigned".
//
// Config note: DetectionFrame carries only gate_id today — site scoping
// is applied server-side (FieldControllerServiceImpl::SubmitDetection
// stamps its configured site onto the AuthorizeRequest) and lane_id is
// reserved for the commercial multi-lane profile. Both live here anyway
// so the daemon configures its full identity in one struct and the
// mapping grows with the proto instead of with call sites.

#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "gate_service.pb.h"
#include "vision/observation.hpp"

namespace gate::vision {

struct FrameBuilderConfig {
    std::string site_id;  // Context only until the proto grows the field
    std::string gate_id;  // Mapped onto DetectionFrame.gate_id
    std::string lane_id;  // Reserved (commercial multi-lane profile)
};

class FrameBuilder {
public:
    explicit FrameBuilder(FrameBuilderConfig cfg) : cfg_(std::move(cfg)) {}

    // Field-faithful conversion; assigns the next monotonic frame_id.
    gate::v1::DetectionFrame build(const ObservationSet& obs);

    // frame_id of the most recent build(); 0 if none yet.
    std::uint64_t last_frame_id() const { return next_frame_id_ - 1; }

    const FrameBuilderConfig& config() const { return cfg_; }

private:
    FrameBuilderConfig cfg_;
    std::uint64_t next_frame_id_ = 1;
};

// Exposed for tests and future drivers that log wire values.
gate::v1::VehicleClass to_proto_class(VehicleClass cls);

}  // namespace gate::vision
