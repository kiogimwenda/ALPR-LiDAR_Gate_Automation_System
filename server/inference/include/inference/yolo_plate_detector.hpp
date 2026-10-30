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
// The deployed model is the open-image-models YOLOv9-s-608 plate detector
// with EfficientNMS_TRT grafted on by scripts/export-models/graft_efficientnms.py
// (input 1x3x608x608); the input size is read from the engine.
//
// The class is single-image (batch == 1). Multi-image batching is a Phase
// 4.3 concern; the current pipeline runs one camera frame at a time.
#pragma once

#include <filesystem>
#include <opencv2/core.hpp>
#include <span>
#include <string>
#include <vector>

#include "inference/trt_engine.hpp"

namespace gate::inference {

// One detection in the original (un-letterboxed) image's coordinate space.
struct PlateDetection {
    cv::Rect2f box;    // x, y, w, h in original-image pixels
    float confidence;  // EfficientNMS_TRT output score, [0, 1]
    int class_id;      // 0 for single-class plate models
};

class YoloPlateDetector {
public:
    struct Config {
        std::filesystem::path engine_path;
        // Letterbox canvas size. 0 = take it from the engine's input tensor
        // ([1, 3, H, W]); set explicitly only to override a dynamic engine.
        int input_width = 0;
        int input_height = 0;
        // EfficientNMS_TRT applies its own (looser, 0.10) score threshold at
        // export time; this floor is the operating point. 0.15 is where the
        // Kenya test set's recall/precision knee sits for YOLOv9-s-608
        // (models/downloads/eval/RESULTS.md).
        float confidence_floor = 0.15f;
        // Tensor names — defaults match the official yolov9 export script.
        std::string input_name = "images";
        std::string num_dets_name = "num_dets";
        std::string boxes_name = "det_boxes";
        std::string scores_name = "det_scores";
        std::string classes_name = "det_classes";
    };

    // Build the detector. Throws TrtException on engine load failure or if
    // the engine doesn't expose the four expected output tensors.
    static YoloPlateDetector load(Config cfg, nvinfer1::ILogger& logger);

    YoloPlateDetector(YoloPlateDetector&&) noexcept = default;
    YoloPlateDetector& operator=(YoloPlateDetector&&) noexcept = default;
    YoloPlateDetector(const YoloPlateDetector&) = delete;
    YoloPlateDetector& operator=(const YoloPlateDetector&) = delete;

    // Run detection on a single BGR image. Returns detections sorted by
    // descending confidence, in the input image's pixel coordinate frame.
    [[nodiscard]] std::vector<PlateDetection> detect(const cv::Mat& bgr);

    // Native engine handle for advanced use (e.g. CUDA-graph capture later).
    [[nodiscard]] TrtEngine& engine() noexcept { return engine_; }
    [[nodiscard]] const TrtEngine& engine() const noexcept { return engine_; }

    [[nodiscard]] const Config& config() const noexcept { return cfg_; }

    // The preprocessed 1x3xHxW tensor of the most recent detect() call
    // (letterboxed, RGB, /255, CHW). For offline parity checks (alpr_bench).
    [[nodiscard]] std::span<const float> last_input() const noexcept { return input_chw_; }

private:
    YoloPlateDetector(TrtEngine&& engine, Config cfg);

    // Letterbox-resize `src` into a (cfg_.input_width × cfg_.input_height)
    // canvas, preserving aspect ratio and padding with neutral gray (114).
    // Records the scale and padding so detection boxes can be unmapped.
    struct LetterboxParams {
        float scale;  // src px → input px
        float pad_x;  // pixels of padding on left side of input canvas
        float pad_y;  // pixels of padding on top side of input canvas
    };
    LetterboxParams letterbox_canvas(const cv::Mat& src, cv::Mat& dst) const;

    TrtEngine engine_;
    Config cfg_;

    // Pre-allocated host scratch; sized once at load time, reused per frame.
    std::vector<float> input_chw_;        // 3 * H * W  floats
    std::vector<int32_t> num_dets_host_;  // 1 int
    std::vector<float> boxes_host_;       // max_det * 4 floats
    std::vector<float> scores_host_;      // max_det floats
    std::vector<int32_t> classes_host_;   // max_det ints

    int max_detections_ = 0;  // resolved from the engine's det_boxes shape
};

}  // namespace gate::inference
