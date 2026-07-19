// paddle_ocr_recognizer.hpp
//
// PaddleOCR PP-OCRv4 (or compatible) plate-text recognizer backed by a
// TensorRT engine. Targets the standard CRNN export:
//
//   input  "x" : float32 NCHW, default 1×3×48×320 (NHWC after preprocess
//                normalization with mean/std).
//   output     : float32 [N, T, C] post-softmax logits where
//                   T = sequence length (typically 40 or 80)
//                   C = dictionary size + 1 blank token at index 0
//
// CTC-decoded into (text, mean per-char confidence).
//
// The recognizer is single-image. A batch overload runs the loop on the host;
// true batched inference is a Phase 4.2.4 pipeline-orchestration concern.
#pragma once

#include <array>
#include <filesystem>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "inference/trt_engine.hpp"

namespace gate::inference {

struct RecognizedPlate {
    std::string text;  // CTC-decoded plate string
    float confidence;  // mean of per-char output probabilities, [0, 1]
};

class PaddleOcrRecognizer {
public:
    struct Config {
        std::filesystem::path engine_path;
        // One UTF-8 character per line. Index 0 in the model output is the
        // CTC blank token; index i+1 corresponds to dictionary line i.
        std::filesystem::path dictionary_path;

        int input_width = 320;
        int input_height = 48;
        // PP-OCRv4 default normalization: pixel/255 → (x - mean) / std.
        std::array<float, 3> mean = {0.5f, 0.5f, 0.5f};
        std::array<float, 3> stddev = {0.5f, 0.5f, 0.5f};

        // Tensor names — defaults match the official PaddleOCR ONNX export.
        std::string input_name = "x";
        std::string output_name = "softmax_2.tmp_0";
    };

    // Build the recognizer. Throws TrtException on engine load failure or
    // dictionary read failure.
    static PaddleOcrRecognizer load(Config cfg, nvinfer1::ILogger& logger);

    PaddleOcrRecognizer(PaddleOcrRecognizer&&) noexcept = default;
    PaddleOcrRecognizer& operator=(PaddleOcrRecognizer&&) noexcept = default;
    PaddleOcrRecognizer(const PaddleOcrRecognizer&) = delete;
    PaddleOcrRecognizer& operator=(const PaddleOcrRecognizer&) = delete;

    // Recognize a single cropped plate image (BGR). Returns empty text
    // and confidence == 0.0 if the model predicts only blanks.
    [[nodiscard]] RecognizedPlate recognize(const cv::Mat& plate_bgr);

    // Convenience batch overload — runs the single-image path in a loop.
    // Useful when the ALPR pipeline has multiple plates per frame.
    [[nodiscard]] std::vector<RecognizedPlate> recognize_batch(const std::vector<cv::Mat>& plates);

    [[nodiscard]] TrtEngine& engine() noexcept { return engine_; }
    [[nodiscard]] const TrtEngine& engine() const noexcept { return engine_; }
    [[nodiscard]] const Config& config() const noexcept { return cfg_; }
    [[nodiscard]] std::size_t vocabulary_size() const noexcept {
        return dictionary_.size() + 1;  // +1 for the blank
    }

private:
    PaddleOcrRecognizer(TrtEngine&& engine, Config cfg, std::vector<std::string> dictionary);

    // Resize the source plate to the model's input height (preserve aspect
    // ratio), pad/crop to input_width on the right, normalize per channel,
    // and write straight into the contiguous CHW host buffer.
    void preprocess_(const cv::Mat& src, float* dst_chw) const;

    // CTC greedy decode: argmax along the class dim, collapse repeats, drop
    // blanks (index 0). Returns the decoded text plus the mean of the
    // argmax-probabilities of the kept characters.
    RecognizedPlate ctc_decode_(const float* logits) const;

    TrtEngine engine_;
    Config cfg_;
    std::vector<std::string> dictionary_;  // index i → character at line i

    int seq_len_ = 0;      // T from output shape
    int num_classes_ = 0;  // C from output shape (dictionary_.size() + 1)

    std::vector<float> input_chw_;      // 3 * input_height * input_width
    std::vector<float> output_logits_;  // T * C
};

}  // namespace gate::inference
