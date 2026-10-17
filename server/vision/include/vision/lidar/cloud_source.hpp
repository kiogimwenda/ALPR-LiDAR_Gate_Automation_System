// cloud_source.hpp — the LiDAR driver seam (Phase 6.1).
//
// The point-cloud twin of ObservationSource: a pull interface every
// LiDAR driver implements (Unitree L1, PCD replay, synthetic). next()
// blocks until the next batch and returns nullopt at end of stream or
// when the device is gone; failure() then says which (empty = clean
// end, e.g. a replay that ran out).

#pragma once

#include <optional>
#include <string>

#include "vision/lidar/point_cloud.hpp"

namespace gate::vision::lidar {

class CloudSource {
public:
    virtual ~CloudSource() = default;

    virtual std::optional<PointCloud> next() = 0;

    // Why the stream ended; empty while running or after a clean end.
    [[nodiscard]] virtual std::string failure() const { return {}; }

    // Short human-readable description for logs ("unitree-l1 /dev/ttyUSB0").
    [[nodiscard]] virtual std::string describe() const = 0;
};

}  // namespace gate::vision::lidar
