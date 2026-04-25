// alpr_pipeline.cpp

#include "inference/alpr_pipeline.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace gate::inference {

AlprPipeline::AlprPipeline(YoloPlateDetector&& det, PaddleOcrRecognizer&& rec, Config cfg)
    : detector_(std::move(det)), recognizer_(std::move(rec)), cfg_(std::move(cfg)) {}

AlprPipeline AlprPipeline::load(Config cfg, nvinfer1::ILogger& logger) {
    auto det = YoloPlateDetector::load(cfg.detector, logger);
    auto rec = PaddleOcrRecognizer::load(cfg.recognizer, logger);

    spdlog::info("AlprPipeline ready: max_plates={}, crop_margin={:.3f}, ocr_floor={:.2f}",
                 cfg.max_plates_per_frame, cfg.crop_margin, cfg.ocr_confidence_floor);

    return AlprPipeline{std::move(det), std::move(rec), std::move(cfg)};
}

cv::Rect AlprPipeline::expand_and_clamp_(const cv::Rect2f& box, const cv::Size& frame_size) const {
    const float dx = box.width * cfg_.crop_margin;
    const float dy = box.height * cfg_.crop_margin;

    float x1 = box.x - dx;
    float y1 = box.y - dy;
    float x2 = box.x + box.width + dx;
    float y2 = box.y + box.height + dy;

    x1 = std::clamp(x1, 0.0f, static_cast<float>(frame_size.width));
    y1 = std::clamp(y1, 0.0f, static_cast<float>(frame_size.height));
    x2 = std::clamp(x2, 0.0f, static_cast<float>(frame_size.width));
    y2 = std::clamp(y2, 0.0f, static_cast<float>(frame_size.height));

    const int xi = static_cast<int>(std::floor(x1));
    const int yi = static_cast<int>(std::floor(y1));
    const int wi = static_cast<int>(std::ceil(x2 - x1));
    const int hi = static_cast<int>(std::ceil(y2 - y1));
    return cv::Rect(xi, yi, std::max(1, wi), std::max(1, hi));
}

std::vector<PlateReading> AlprPipeline::process(const cv::Mat& frame_bgr) {
    auto detections = detector_.detect(frame_bgr);
    if (detections.empty()) {
        return {};
    }

    // YoloPlateDetector::detect already sorts by descending confidence, so
    // the cap simply takes the top-N.
    const std::size_t n_keep = std::min<std::size_t>(
        detections.size(), static_cast<std::size_t>(std::max(0, cfg_.max_plates_per_frame)));
    detections.resize(n_keep);

    // Build the crop batch alongside a parallel vector of valid detections.
    // A box that clamps to a degenerate ROI (e.g. a detection touching the
    // frame edge) is dropped — the recognizer would reject an empty Mat.
    std::vector<cv::Mat> crops;
    std::vector<const PlateDetection*> kept;
    crops.reserve(n_keep);
    kept.reserve(n_keep);

    for (const auto& d : detections) {
        const cv::Rect roi = expand_and_clamp_(d.box, frame_bgr.size());
        if (roi.width < 4 || roi.height < 4) {
            continue;  // not enough pixels for OCR to do anything useful
        }
        crops.push_back(frame_bgr(roi).clone());
        kept.push_back(&d);
    }

    if (crops.empty()) {
        return {};
    }

    auto ocr_results = recognizer_.recognize_batch(crops);

    std::vector<PlateReading> readings;
    readings.reserve(kept.size());
    for (std::size_t i = 0; i < kept.size(); ++i) {
        PlateReading r;
        r.box = kept[i]->box;
        r.detection_score = kept[i]->confidence;
        r.text = std::move(ocr_results[i].text);
        r.ocr_score = ocr_results[i].confidence;
        readings.push_back(std::move(r));
    }

    return readings;
}

}  // namespace gate::inference
