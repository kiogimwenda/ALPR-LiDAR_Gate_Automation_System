// version.hpp — gate-firmware version string + build metadata.
//
// The version is bumped manually per-release in src/version.cpp; it gets
// reported in the boot banner and surfaced through the firmware_version
// field of the proto's Telemetry message. CI's build system stamps a git
// short-SHA into the build via the GATE_BUILD_SHA macro when set.

#pragma once

namespace gate::drivers {

// Semantic version, hand-edited at release time. 0.x.y while we're still
// in Phase 4 implementation.
extern const char* const kVersion;

}  // namespace gate::drivers
