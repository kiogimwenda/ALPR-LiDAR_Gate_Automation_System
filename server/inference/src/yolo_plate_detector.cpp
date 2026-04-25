// yolo_plate_detector.cpp

#include "inference/yolo_plate_detector.hpp"

#include <spdlog/spdlog.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstring>

namespace gate::inference {

namespace {

// Span helpers — std::span<std::byte> over a typed buffer.
template <typename T>
std::span<const std::byte> as_bytes(const std::vector<T>& v) noexcept {
    return std::as_bytes(std::span<const T>(v.data(), v.size()));
}
template <typename T>
std::span<std::byte> as_writable_bytes(std::vector<T>& v) noexcept {
    return std::as_writable_bytes(std::span<T>(v.data(), v.size()));
}

}  // namespace

YoloPlateDetector::YoloPlateDetector(TrtEngine&& engine, Config cfg)
    : engine_(std::move(engine)), cfg_(std::move(cfg)) {}

YoloPlateDetector YoloPlateDetector::load(Config cfg, nvinfer1::ILogger& logger) {
    auto engine = TrtEngine::load(cfg.engine_path, logger);

    YoloPlateDetector self{std::move(engine), std::move(cfg)};

    // Pin the input shape now. EfficientNMS_TRT engines are typically
    // exported with a fixed batch=1; calling setInputShape with the
    // configured H×W is a no-op for static engines and a binding for
    // dynamic ones — either way it primes the context for output shape
    // resolution.
    nvinfer1::Dims4 input_dims{1, 3, self.cfg_.input_height, self.cfg_.input_width};
    self.engine_.set_input_shape(self.cfg_.input_name, input_dims);

    // Discover max_detections from the resolved det_boxes shape:
    //   det_boxes : [N, max_det, 4]
    const auto& boxes_io = self.engine_.tensor(self.cfg_.boxes_name);
    if (boxes_io.shape.nbDims < 3) {
        throw TrtException("YoloPlateDetector: '" + self.cfg_.boxes_name +
                           "' has fewer than 3 dims — is this an end2end NMS engine?");
    }
    // Use the context-resolved shape (the engine may have dynamic dims).
    const auto resolved = self.engine_.context()
                              ->getTensorShape(self.cfg_.boxes_name.c_str());
    self.max_detections_ = static_cast<int>(resolved.d[1]);
    if (self.max_detections_ <= 0) {
        throw TrtException("YoloPlateDetector: could not resolve max_detections "
                           "from " + self.cfg_.boxes_name + " shape");
    }

    // Allocate host-side scratch buffers, sized once.
    self.input_chw_.assign(static_cast<std::size_t>(3 * self.cfg_.input_height *
                                                    self.cfg_.input_width), 0.0f);
    self.num_dets_host_.assign(1, 0);
    self.boxes_host_  .assign(static_cast<std::size_t>(self.max_detections_) * 4, 0.0f);
    self.scores_host_ .assign(static_cast<std::size_t>(self.max_detections_),     0.0f);
    self.classes_host_.assign(static_cast<std::size_t>(self.max_detections_),     0);

    spdlog::info("YoloPlateDetector ready: {}x{} input, max_detections={}, conf_floor={}",
                 self.cfg_.input_width, self.cfg_.input_height,
                 self.max_detections_, self.cfg_.confidence_floor);

    return self;
}

YoloPlateDetector::LetterboxParams
YoloPlateDetector::letterbox_(const cv::Mat& src, cv::Mat& dst) const {
    const float src_w = static_cast<float>(src.cols);
    const float src_h = static_cast<float>(src.rows);
    const float dst_w = static_cast<float>(cfg_.input_width);
    const float dst_h = static_cast<float>(cfg_.input_height);

    // Pick the smaller scale so the resized image fits inside the canvas.
    const float scale = std::min(dst_w / src_w, dst_h / src_h);
    const int new_w = static_cast<int>(std::round(src_w * scale));
    const int new_h = static_cast<int>(std::round(src_h * scale));

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

    // Center the resized image inside the canvas; pad with neutral gray
    // (114, 114, 114) — the YOLOv9 / Ultralytics convention.
    const int pad_x = (cfg_.input_width  - new_w) / 2;
    const int pad_y = (cfg_.input_height - new_h) / 2;
    dst.create(cfg_.input_height, cfg_.input_width, src.type());
    dst.setTo(cv::Scalar(114, 114, 114));
    resized.copyTo(dst(cv::Rect(pad_x, pad_y, new_w, new_h)));

    return {scale, static_cast<float>(pad_x), static_cast<float>(pad_y)};
}

std::vector<PlateDetection>
YoloPlateDetector::detect(const cv::Mat& bgr) {
    if (bgr.empty()) {
        throw TrtException("YoloPlateDetector::detect: input image is empty");
    }
    if (bgr.type() != CV_8UC3) {
        throw TrtException("YoloPlateDetector::detect: input must be CV_8UC3 (BGR)");
    }

    // ---- Preprocess ----
    cv::Mat letterboxed;
    const auto lb = letterbox_(bgr, letterboxed);

    // BGR → RGB, uint8 → float32 normalized [0, 1], HWC → CHW.
    cv::Mat rgb;
    cv::cvtColor(letterboxed, rgb, cv::COLOR_BGR2RGB);
    cv::Mat rgb_f32;
    rgb.convertTo(rgb_f32, CV_32FC3, 1.0 / 255.0);

    // Split channels directly into the contiguous CHW host buffer.
    const int plane = cfg_.input_height * cfg_.input_width;
    std::vector<cv::Mat> channels{
        cv::Mat(cfg_.input_height, cfg_.input_width, CV_32FC1, input_chw_.data() + 0 * plane),
        cv::Mat(cfg_.input_height, cfg_.input_width, CV_32FC1, input_chw_.data() + 1 * plane),
        cv::Mat(cfg_.input_height, cfg_.input_width, CV_32FC1, input_chw_.data() + 2 * plane),
    };
    cv::split(rgb_f32, channels);

    // ---- Run ----
    const std::unordered_map<std::string, std::span<const std::byte>> host_in{
        {cfg_.input_name, as_bytes(input_chw_)},
    };
    const std::unordered_map<std::string, std::span<std::byte>> host_out{
        {cfg_.num_dets_name, as_writable_bytes(num_dets_host_)},
        {cfg_.boxes_name,    as_writable_bytes(boxes_host_)},
        {cfg_.scores_name,   as_writable_bytes(scores_host_)},
        {cfg_.classes_name,  as_writable_bytes(classes_host_)},
    };
    engine_.enqueue(host_in, host_out);
    engine_.sync();

    // ---- Decode ----
    const int n = std::min(num_dets_host_[0], max_detections_);
    std::vector<PlateDetection> dets;
    dets.reserve(static_cast<std::size_t>(n));

    for (int i = 0; i < n; ++i) {
        const float score = scores_host_[i];
        if (score < cfg_.confidence_floor) continue;

        // Box is (x1, y1, x2, y2) in letterboxed-input pixel space.
        const float x1_in = boxes_host_[i * 4 + 0];
        const float y1_in = boxes_host_[i * 4 + 1];
        const float x2_in = boxes_host_[i * 4 + 2];
        const float y2_in = boxes_host_[i * 4 + 3];

        // Undo letterbox: subtract pad, divide by scale, clamp to image.
        const float x1 = std::max(0.0f, (x1_in - lb.pad_x) / lb.scale);
        const float y1 = std::max(0.0f, (y1_in - lb.pad_y) / lb.scale);
        const float x2 = std::min(static_cast<float>(bgr.cols),
                                  (x2_in - lb.pad_x) / lb.scale);
        const float y2 = std::min(static_cast<float>(bgr.rows),
                                  (y2_in - lb.pad_y) / lb.scale);
        if (x2 <= x1 || y2 <= y1) continue;  // degenerate after clamping

        PlateDetection d;
        d.box        = cv::Rect2f(x1, y1, x2 - x1, y2 - y1);
        d.confidence = score;
        d.class_id   = classes_host_[i];
        dets.push_back(d);
    }

    // EfficientNMS_TRT already returns detections sorted by score, but the
    // confidence-floor filter above can leave gaps; sort once for safety.
    std::sort(dets.begin(), dets.end(),
              [](const PlateDetection& a, const PlateDetection& b) {
                  return a.confidence > b.confidence;
              });
    return dets;
}

}  // namespace gate::inference
