// alpr_pipeline.cpp

#include "inference/alpr_pipeline.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "inference/detail/cpu_algorithms.hpp"

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
    return detail::expand_box_with_margin(box, frame_size, cfg_.crop_margin, /*min_extent=*/1);
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
