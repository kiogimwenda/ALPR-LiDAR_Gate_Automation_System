// detail/engine_compat.hpp
//
// GPU / TensorRT compatibility rules for serialized engines (.plan files).
//
// A TensorRT engine only runs where it was built for:
//   * hardware_compat "none" (the default) or "same_cc" — exactly the
//     compute capability of the build GPU;
//   * hardware_compat "ampere_plus" — any GPU with compute capability >= 8.0.
//     There is no TensorRT level that also covers Turing (RTX 20, SM 7.5),
//     which is why the deployment builds engines on the target host
//     (scripts/export-models/build_engines.sh).
// In every mode the TensorRT library version must match the builder's.
//
// The engine builder writes a `<plan>.meta` sidecar (simple `key=value`
// lines) recording what it built for. These checks turn a mismatch into
// an actionable message *before* deserialization, instead of TensorRT's
// opaque "deserializeCudaEngine failed".
//
// Header-only and standard-library-only so CPU-only CI can unit-test it.
#pragma once

#include <istream>
#include <optional>
#include <string>

namespace gate::inference::detail {

// Oldest GPU generation the system supports: Turing (RTX 20 series).
inline constexpr int kMinComputeMajor = 7;
inline constexpr int kMinComputeMinor = 5;

// What an engine was built for, as recorded in its `.meta` sidecar.
struct EngineMeta {
    int cc_major = -1;  // compute capability of the build GPU
    int cc_minor = -1;
    std::string gpu;              // build GPU name (informational)
    std::string tensorrt;         // "major.minor.patch" of the builder
    std::string hardware_compat;  // "none" | "same_cc" | "ampere_plus"
};

// Parse `key=value` lines; unknown keys and '#' comments are ignored.
// Missing or malformed fields keep their defaults.
inline EngineMeta parse_engine_meta(std::istream& in) {
    EngineMeta m;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "compute_capability") {
            const auto dot = val.find('.');
            if (dot != std::string::npos) {
                try {
                    m.cc_major = std::stoi(val.substr(0, dot));
                    m.cc_minor = std::stoi(val.substr(dot + 1));
                } catch (...) {
                    m.cc_major = m.cc_minor = -1;
                }
            }
        } else if (key == "gpu") {
            m.gpu = val;
        } else if (key == "tensorrt") {
            m.tensorrt = val;
        } else if (key == "hardware_compat") {
            m.hardware_compat = val;
        }
    }
    return m;
}

// "sm_75"-style label for log messages.
inline std::string sm_label(int major, int minor) {
    return "sm_" + std::to_string(major) + std::to_string(minor);
}

// Is the running GPU new enough for the system at all?
inline std::optional<std::string> check_device_supported(const std::string& gpu, int major,
                                                         int minor) {
    if (major > kMinComputeMajor || (major == kMinComputeMajor && minor >= kMinComputeMinor))
        return std::nullopt;
    return "GPU '" + gpu + "' (" + sm_label(major, minor) +
           ") is older than the minimum supported generation: an NVIDIA RTX 20-series "
           "(Turing, sm_75) or newer GPU is required";
}

// Can an engine described by `meta` run on this GPU + TensorRT build?
// `trt_version` is "major.minor.patch" of the linked TensorRT library.
inline std::optional<std::string> check_engine_compat(const EngineMeta& meta,
                                                      const std::string& gpu, int major, int minor,
                                                      const std::string& trt_version) {
    const std::string rebuild =
        " — rebuild the engines on this host: scripts/export-models/build_engines.sh";
    if (!meta.tensorrt.empty() && meta.tensorrt != trt_version) {
        return "engine was built with TensorRT " + meta.tensorrt + " but this host runs " +
               trt_version + rebuild;
    }
    if (meta.cc_major < 0)
        return std::nullopt;  // no build-GPU record: let TensorRT decide
    const std::string built_for =
        "'" + meta.gpu + "' (" + sm_label(meta.cc_major, meta.cc_minor) + ")";
    const std::string here = "'" + gpu + "' (" + sm_label(major, minor) + ")";
    if (meta.hardware_compat == "ampere_plus") {
        if (major < 8) {
            return "engine was built Ampere-plus portable (needs sm_80 or newer) and cannot run "
                   "on " +
                   here + rebuild;
        }
        return std::nullopt;
    }
    if (meta.cc_major != major || meta.cc_minor != minor) {
        return "engine was built for " + built_for + " and cannot run on " + here + rebuild;
    }
    return std::nullopt;
}

}  // namespace gate::inference::detail
