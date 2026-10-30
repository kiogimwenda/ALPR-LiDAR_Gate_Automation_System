// plate_recognizer.hpp
//
// License-plate text recognizer backed by a TensorRT engine. Two model
// families are supported, chosen by Config::mode:
//
//   kCtc    PaddleOCR PP-OCR rec (CRNN + CTC)
//             input  float32 NCHW [1, 3, H, W]   (default 48 x 320)
//                    aspect-preserving resize to H, right-padded to W with 0
//                    AFTER normalisation (x/255 - mean) / std, BGR order
//             output float32 [1, T, C] post-softmax, C = dictionary + 1,
//                    blank at class 0; greedy CTC decode
//   kSlots  fast-plate-ocr CCT (one classifier head per character slot)
//             input  uint8 NHWC [1, H, W, 3] RGB    (default 64 x 128)
//                    plain stretch to W x H, no aspect keep, no normalisation
//             output float32 [1, S, C] post-softmax ("plate"), C = alphabet
//                    size including the pad character; per-slot argmax,
//                    pad slots dropped, repeats kept. Any other outputs
//                    (the "region" head) are bound but ignored.
//
// The recognizer is single-image; recognize_batch() loops on the host.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "inference/trt_engine.hpp"

namespace gate::inference {

struct RecognizedPlate {
    std::string text;  // decoded plate string, raw (no format normalisation)
    float confidence;  // mean of per-char output probabilities, [0, 1]
};

class PlateRecognizer {
public:
    enum class Mode : std::uint8_t { kCtc, kSlots };

    struct Config {
        std::filesystem::path engine_path;
        Mode mode = Mode::kCtc;

        // kCtc: one UTF-8 character per line; line i is model class i + 1.
        std::filesystem::path dictionary_path;
        // kSlots: fast-plate-ocr plate_config.yaml (alphabet, pad_char, slots,
        // image size). Image size from here wins over input_width/height.
        std::filesystem::path plate_config_path;

        // Input size. 0 = take it from the engine's input tensor shape.
        int input_width = 0;
        int input_height = 0;

        // kCtc normalisation: pixel/255 → (x - mean) / std, per BGR channel.
        std::array<float, 3> mean = {0.5f, 0.5f, 0.5f};
        std::array<float, 3> stddev = {0.5f, 0.5f, 0.5f};

        // Tensor names. Empty = the engine's only input / for kSlots the
        // output named "plate", else the first output.
        std::string input_name;
        std::string output_name;
    };

    // Build the recognizer. Throws TrtException on engine load failure, an
    // unreadable dictionary / plate config, or a model whose class count or
    // input layout does not match the configured mode.
    static PlateRecognizer load(Config cfg, nvinfer1::ILogger& logger);

    PlateRecognizer(PlateRecognizer&&) noexcept = default;
    PlateRecognizer& operator=(PlateRecognizer&&) noexcept = default;
    PlateRecognizer(const PlateRecognizer&) = delete;
    PlateRecognizer& operator=(const PlateRecognizer&) = delete;

    // Recognize a single cropped plate image (BGR). Returns empty text and
    // confidence == 0 if every step / slot decodes to blank / pad.
    [[nodiscard]] RecognizedPlate recognize(const cv::Mat& plate_bgr);
    [[nodiscard]] std::vector<RecognizedPlate> recognize_batch(const std::vector<cv::Mat>& plates);

    [[nodiscard]] TrtEngine& engine() noexcept { return engine_; }
    [[nodiscard]] const Config& config() const noexcept { return cfg_; }

private:
    PlateRecognizer(TrtEngine&& engine, Config cfg);

    TrtEngine engine_;
    Config cfg_;

    std::vector<std::string> dictionary_;  // kCtc
    std::string alphabet_;                 // kSlots
    char pad_char_ = '_';                  // kSlots

    int steps_ = 0;        // T (kCtc) or S (kSlots)
    int num_classes_ = 0;  // C

    std::vector<float> input_f32_;
    std::vector<std::uint8_t> input_u8_;
    std::vector<float> output_;
};

}  // namespace gate::inference
