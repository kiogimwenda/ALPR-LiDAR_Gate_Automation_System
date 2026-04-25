// alpr_pipeline.hpp
//
// End-to-end ALPR pipeline: full camera frame → license-plate readings.
//
//   YoloPlateDetector ──► (crop each box w/ margin) ──► PaddleOcrRecognizer
//
// The pipeline owns both backends and exposes a single `process(frame)` call
// that returns the per-plate detection geometry alongside the OCR text. It is
// the unit the gate-control logic talks to; nothing above this layer needs to
// know about TensorRT, letterboxing, CTC, or which model produced what.
#pragma once

#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "inference/paddle_ocr_recognizer.hpp"
#include "inference/yolo_plate_detector.hpp"

namespace gate::inference {

// One read plate, with both detection geometry and OCR result attached.
struct PlateReading {
    cv::Rect2f box;         // detector box in original-frame pixels
    std::string text;       // OCR-decoded plate string ("" if recognizer rejected)
    float detection_score;  // YOLO confidence, [0, 1]
    float ocr_score;        // recognizer mean per-char confidence, [0, 1]
};

class AlprPipeline {
public:
    struct Config {
        YoloPlateDetector::Config detector;
        PaddleOcrRecognizer::Config recognizer;

        // Pad each detector box outward by this fraction of its width/height
        // before cropping for OCR. CRNN models expect a small border around
        // the plate; cropping flush to the YOLO box trims edge characters.
        float crop_margin = 0.08f;

        // Cap how many top-scored detections per frame are sent to OCR. Real
        // gate frames see 1-2 plates; the cap protects latency on noisy frames
        // where the detector returns dozens of low-confidence boxes the OCR
        // pass would otherwise spend cycles on.
        int max_plates_per_frame = 8;

        // Reject OCR results below this mean per-char confidence. Decoded text
        // is still returned (so callers can log low-confidence reads) but the
        // ocr_score field lets downstream code apply its own gate-control
        // policy without re-deriving the threshold.
        float ocr_confidence_floor = 0.0f;
    };

    // Build the pipeline. Loads both engines through their own load() factories
    // and propagates any TrtException upward unchanged.
    static AlprPipeline load(Config cfg, nvinfer1::ILogger& logger);

    AlprPipeline(AlprPipeline&&) noexcept = default;
    AlprPipeline& operator=(AlprPipeline&&) noexcept = default;
    AlprPipeline(const AlprPipeline&) = delete;
    AlprPipeline& operator=(const AlprPipeline&) = delete;

    // Process a single camera frame (BGR). Returns one entry per detection
    // that survived `max_plates_per_frame`, sorted by detection_score desc.
    // Plates whose OCR score is below `ocr_confidence_floor` are still
    // returned — the policy decision is left to the caller.
    [[nodiscard]] std::vector<PlateReading> process(const cv::Mat& frame_bgr);

    [[nodiscard]] YoloPlateDetector& detector() noexcept { return detector_; }
    [[nodiscard]] PaddleOcrRecognizer& recognizer() noexcept { return recognizer_; }
    [[nodiscard]] const Config& config() const noexcept { return cfg_; }

private:
    AlprPipeline(YoloPlateDetector&& det, PaddleOcrRecognizer&& rec, Config cfg);

    // Expand `box` by `crop_margin` on each side and clamp to the frame.
    // Returns the safe ROI in pixel coordinates.
    cv::Rect expand_and_clamp_(const cv::Rect2f& box, const cv::Size& frame_size) const;

    YoloPlateDetector detector_;
    PaddleOcrRecognizer recognizer_;
    Config cfg_;
};

}  // namespace gate::inference
