// camera_source.cpp — VideoCapture → AlprPipeline → ObservationSet.

#include "vision/camera_source.hpp"

#include <algorithm>
#include <cmath>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>
#include <utility>

namespace gate::vision {

namespace {

// AlprPipeline boxes are float rects in original-frame pixels; the wire
// wants unsigned pixel coordinates. Clamp at zero — a letterboxed
// detection can poke a fraction of a pixel outside the frame.
PixelBox to_pixel_box(const cv::Rect2f& r) {
    PixelBox out;
    out.x = static_cast<std::uint32_t>(std::max(0.0f, std::floor(r.x)));
    out.y = static_cast<std::uint32_t>(std::max(0.0f, std::floor(r.y)));
    out.width = static_cast<std::uint32_t>(std::max(0.0f, std::ceil(r.width)));
    out.height = static_cast<std::uint32_t>(std::max(0.0f, std::ceil(r.height)));
    return out;
}

}  // namespace

CameraSource CameraSource::open(Config cfg) {
    if (cfg.uri.empty()) {
        throw std::runtime_error("camera_source: uri is required");
    }

    auto cap = std::make_unique<cv::VideoCapture>();
    // Numeric URIs mean a local V4L2 device; everything else (rtsp://,
    // file paths) goes through the default backend resolution.
    const bool is_index =
        !cfg.uri.empty() && cfg.uri.find_first_not_of("0123456789") == std::string::npos;
    const bool opened = is_index ? cap->open(std::stoi(cfg.uri)) : cap->open(cfg.uri);
    if (!opened || !cap->isOpened()) {
        // A camera that isn't there must stop the daemon, not produce an
        // eternally-empty driveway.
        throw std::runtime_error("camera_source: cannot open capture: " + cfg.uri);
    }

    auto logger = std::make_unique<gate::inference::TrtLogger>();
    auto pipeline = gate::inference::AlprPipeline::load(cfg.alpr, *logger);
    spdlog::info("camera_source: capture open ({}), engines loaded (detector={}, recognizer={})",
                 cfg.uri, cfg.alpr.detector.engine_path.string(),
                 cfg.alpr.recognizer.engine_path.string());

    return CameraSource{std::move(cfg), std::move(logger), std::move(cap), std::move(pipeline)};
}

CameraSource::CameraSource(Config cfg, std::unique_ptr<gate::inference::TrtLogger> logger,
                           std::unique_ptr<cv::VideoCapture> cap,
                           gate::inference::AlprPipeline pipeline)
    : cfg_(std::move(cfg)),
      trt_logger_(std::move(logger)),
      cap_(std::move(cap)),
      pipeline_(std::move(pipeline)) {}

CameraSource::CameraSource(CameraSource&&) noexcept = default;
CameraSource& CameraSource::operator=(CameraSource&&) noexcept = default;
CameraSource::~CameraSource() = default;

std::optional<ObservationSet> CameraSource::next() {
    if (cfg_.max_frames != 0 && frames_emitted_ >= cfg_.max_frames) {
        return std::nullopt;
    }

    // Throttle to the configured cadence. The deadline advances from
    // the previous one (not from "now") so inference time doesn't
    // stretch the interval.
    const auto now = std::chrono::steady_clock::now();
    if (next_due_ == std::chrono::steady_clock::time_point{}) {
        next_due_ = now;
    }
    if (next_due_ > now) {
        std::this_thread::sleep_until(next_due_);
    }
    next_due_ += std::chrono::milliseconds{cfg_.frame_interval_ms};

    cv::Mat frame;
    std::uint32_t failures = 0;
    while (!cap_->read(frame) || frame.empty()) {
        if (++failures > cfg_.read_retries) {
            spdlog::error("camera_source: capture dead after {} failed reads — ending stream",
                          failures - 1);
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    ObservationSet out;
    out.capture_ts = std::chrono::system_clock::now();
    for (const auto& reading : pipeline_.process(frame)) {
        // Recognizer-rejected boxes carry empty text — real pixels, no
        // plate string. They can't match an allowlist and would only
        // generate deny-noise server-side; the detection is still
        // visible in the daemon's debug log via the pipeline itself.
        if (reading.text.empty()) {
            continue;
        }
        PlateObservation p;
        p.text = reading.text;
        p.detection_conf = reading.detection_score;
        p.ocr_conf = reading.ocr_score;
        p.box = to_pixel_box(reading.box);
        out.plates.push_back(std::move(p));
    }
    ++frames_emitted_;
    return out;
}

}  // namespace gate::vision
