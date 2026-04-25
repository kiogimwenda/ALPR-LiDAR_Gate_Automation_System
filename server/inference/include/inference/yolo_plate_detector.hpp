// yolo_plate_detector.hpp
//
// YOLOv9 license-plate detector backed by a TensorRT engine. Targets ONNX
// exports that include the EfficientNMS_TRT plugin (the `--end2end` export
// path of the official yolov9 repo), so the engine itself emits already-NMSed
// detections via the standard four output tensors:
//
//   num_dets    : int32  [N, 1]
//   det_boxes   : float  [N, max_det, 4]   (x1, y1, x2, y2 in input space)
//   det_scores  : float  [N, max_det]
//   det_classes : int32  [N, max_det]
//
// The class is single-image (batch == 1). Multi-image batching is a Phase
// 4.3 concern; the current pipeline runs one camera frame at a time.
#pragma once

#include "inference/trt_engine.hpp"

#include <opencv2/core.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace gate::inference {

// One detection in the original (un-letterboxed) image's coordinate space.
struct PlateDetection {
    cv::Rect2f box;         // x, y, w, h in original-image pixels
    float      confidence;  // EfficientNMS_TRT output score, [0, 1]
    int        class_id;    // 0 for single-class plate models
};

class YoloPlateDetector {
public:
    struct Config {
        std::filesystem::path engine_path;
        int                   input_width    = 640;
        int                   input_height   = 640;
        // EfficientNMS_TRT applies its own confidence/IoU thresholds at
        // export time; this is a defense-in-depth filter on the way back out
        // for engines exported with looser settings than the deployment wants.
        float                 confidence_floor = 0.25f;
        // Tensor names — defaults match the official yolov9 export script.
        std::string           input_name        = "images";
        std::string           num_dets_name     = "num_dets";
        std::string           boxes_name        = "det_boxes";
        std::string           scores_name       = "det_scores";
        std::string           classes_name      = "det_classes";
    };

    // Build the detector. Throws TrtException on engine load failure or if
    // the engine doesn't expose the four expected output tensors.
    static YoloPlateDetector load(Config cfg, nvinfer1::ILogger& logger);

    YoloPlateDetector(YoloPlateDetector&&) noexcept            = default;
    YoloPlateDetector& operator=(YoloPlateDetector&&) noexcept = default;
    YoloPlateDetector(const YoloPlateDetector&)                = delete;
    YoloPlateDetector& operator=(const YoloPlateDetector&)     = delete;

    // Run detection on a single BGR image. Returns detections sorted by
    // descending confidence, in the input image's pixel coordinate frame.
    [[nodiscard]] std::vector<PlateDetection> detect(const cv::Mat& bgr);

    // Native engine handle for advanced use (e.g. CUDA-graph capture later).
    [[nodiscard]] TrtEngine&       engine()       noexcept { return engine_; }
    [[nodiscard]] const TrtEngine& engine() const noexcept { return engine_; }

    [[nodiscard]] const Config&    config() const noexcept { return cfg_; }

private:
    YoloPlateDetector(TrtEngine&& engine, Config cfg);

    // Letterbox-resize `src` into a (cfg_.input_width × cfg_.input_height)
    // canvas, preserving aspect ratio and padding with neutral gray (114).
    // Records the scale and padding so detection boxes can be unmapped.
    struct LetterboxParams {
        float scale;     // src px → input px
        float pad_x;     // pixels of padding on left side of input canvas
        float pad_y;     // pixels of padding on top side of input canvas
    };
    LetterboxParams letterbox_(const cv::Mat& src, cv::Mat& dst) const;

    TrtEngine engine_;
    Config    cfg_;

    // Pre-allocated host scratch; sized once at load time, reused per frame.
    std::vector<float>   input_chw_;       // 3 * H * W  floats
    std::vector<int32_t> num_dets_host_;   // 1 int
    std::vector<float>   boxes_host_;      // max_det * 4 floats
    std::vector<float>   scores_host_;     // max_det floats
    std::vector<int32_t> classes_host_;    // max_det ints

    int max_detections_ = 0;  // resolved from the engine's det_boxes shape
};

}  // namespace gate::inference
