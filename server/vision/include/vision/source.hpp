// source.hpp — the sensor-agnostic observation seam (Phase 5.2).
//
// ObservationSource is the single interface the gate-vision daemon pulls
// frames through. Today the only implementation is ScenarioSource (a
// scripted JSON replay); the next milestone's CameraSource (RTSP +
// TensorRT) and the fusion-sensor driver implement the same contract,
// so the daemon's submit loop never learns which sensor is behind it.
//
// Pull model on purpose: the caller owns pacing and lifetime — a source
// blocks in next() until it has a frame (or its script says the frame
// is due) and signals end-of-stream with nullopt. No callbacks, no
// threads leak out of the source.

#pragma once

#include <optional>

#include "vision/observation.hpp"

namespace gate::vision {

class ObservationSource {
public:
    virtual ~ObservationSource() = default;

    // Blocks until the next observation set is available. nullopt means
    // the stream has ended (script exhausted, camera closed) and next()
    // will never yield again.
    virtual std::optional<ObservationSet> next() = 0;
};

}  // namespace gate::vision
