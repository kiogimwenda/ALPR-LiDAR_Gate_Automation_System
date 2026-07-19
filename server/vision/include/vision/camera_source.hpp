// camera_source.hpp — RTSP/file camera → ALPR ObservationSource (Phase 5.3).
//
// The first real sensor behind the Phase 5.2 seam: pulls BGR frames
// from an OpenCV VideoCapture (the ADR-007 Hikvision's RTSP URL, a
// recorded clip for replay, or a V4L2 device index) and runs each one
// through the TensorRT AlprPipeline, emitting the plate readings as an
// ObservationSet. GPU builds only — this header drags in OpenCV and
// TensorRT, so it is compiled solely into `gate_vision_camera`
// (ENABLE_GPU) and included by the daemon behind GATE_VISION_HAVE_CAMERA.
//
// Vehicles are deliberately absent from this source's output: LiDAR
// point-cloud capture (Unitree L1 UDP) is bench-phase work, and the
// eventual ADR-012 fusion unit replaces this driver wholesale. Plate-only
// frames are valid input — the fusion ladder applies its plate-only
// policy (class checks simply have nothing to match).
//
// Fail-closed like every source: an URI that won't open or an engine
// that won't load throws at construction. A capture that goes dark
// mid-run ends the stream (nullopt) after `read_retries` failed grabs —
// the daemon exits non-zero and systemd restarts it, which also re-runs
// the RTSP handshake; limping along silently frame-less would look
// exactly like an empty driveway.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "inference/alpr_pipeline.hpp"
#include "vision/source.hpp"

namespace cv {
class VideoCapture;
}  // namespace cv

namespace gate::vision {

class CameraSource final : public ObservationSource {
public:
    struct Config {
        // Anything cv::VideoCapture accepts: "rtsp://…", a video file
        // path, or a numeric string ("0") for a local V4L2 device.
        std::string uri;

        gate::inference::AlprPipeline::Config alpr;

        // Pace ALPR at gate-relevant cadence instead of camera FPS — a
        // 25 fps stream at 5 fps inference is ample for a vehicle
        // rolling up to a gate and keeps the GPU free for other lanes.
        std::uint32_t frame_interval_ms = 200;

        // Consecutive failed grabs before the stream is declared dead.
        std::uint32_t read_retries = 25;

        // Stop after this many frames (0 = unbounded). Lets a recorded
        // clip drive a bounded validation pass.
        std::uint64_t max_frames = 0;
    };

    // Opens the capture and loads both TensorRT engines; throws
    // std::runtime_error / TrtException on any failure (fail-closed).
    static CameraSource open(Config cfg);

    CameraSource(CameraSource&&) noexcept;
    CameraSource& operator=(CameraSource&&) noexcept;
    ~CameraSource() override;

    // Grabs, throttles, infers. nullopt = stream ended (max_frames
    // reached or the capture died past its retry budget).
    std::optional<ObservationSet> next() override;

private:
    CameraSource(Config cfg, std::unique_ptr<gate::inference::TrtLogger> logger,
                 std::unique_ptr<cv::VideoCapture> cap, gate::inference::AlprPipeline pipeline);

    Config cfg_;
    // Declared before pipeline_ on purpose: TrtEngine keeps a pointer to
    // the logger, so it must outlive the pipeline (members destroy in
    // reverse declaration order).
    std::unique_ptr<gate::inference::TrtLogger> trt_logger_;
    std::unique_ptr<cv::VideoCapture> cap_;
    gate::inference::AlprPipeline pipeline_;
    std::uint64_t frames_emitted_ = 0;
    std::chrono::steady_clock::time_point next_due_{};
};

}  // namespace gate::vision
