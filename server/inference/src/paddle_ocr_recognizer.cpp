// paddle_ocr_recognizer.cpp

#include "inference/paddle_ocr_recognizer.hpp"

#include <spdlog/spdlog.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <fstream>

namespace gate::inference {

namespace {

template <typename T>
std::span<const std::byte> as_bytes(const std::vector<T>& v) noexcept {
    return std::as_bytes(std::span<const T>(v.data(), v.size()));
}
template <typename T>
std::span<std::byte> as_writable_bytes(std::vector<T>& v) noexcept {
    return std::as_writable_bytes(std::span<T>(v.data(), v.size()));
}

std::vector<std::string> read_dictionary(const std::filesystem::path& path) {
    std::ifstream f{path};
    if (!f) {
        throw TrtException("PaddleOcrRecognizer: cannot open dictionary: " + path.string());
    }
    std::vector<std::string> chars;
    std::string line;
    while (std::getline(f, line)) {
        // PaddleOCR dictionaries use one character per line. We keep the line
        // verbatim (no trim) so leading/trailing space chars in the dict stay
        // meaningful — but we do drop a trailing CR for files saved on Windows.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        chars.push_back(std::move(line));
    }
    if (chars.empty()) {
        throw TrtException("PaddleOcrRecognizer: dictionary is empty: " + path.string());
    }
    return chars;
}

}  // namespace

PaddleOcrRecognizer::PaddleOcrRecognizer(TrtEngine&& engine, Config cfg,
                                         std::vector<std::string> dictionary)
    : engine_(std::move(engine)),
      cfg_(std::move(cfg)),
      dictionary_(std::move(dictionary)) {}

PaddleOcrRecognizer
PaddleOcrRecognizer::load(Config cfg, nvinfer1::ILogger& logger) {
    auto dict   = read_dictionary(cfg.dictionary_path);
    auto engine = TrtEngine::load(cfg.engine_path, logger);

    PaddleOcrRecognizer self{std::move(engine), std::move(cfg), std::move(dict)};

    // Pin the input shape so dynamic-shape engines resolve their output dims.
    nvinfer1::Dims4 input_dims{1, 3, self.cfg_.input_height, self.cfg_.input_width};
    self.engine_.set_input_shape(self.cfg_.input_name, input_dims);

    // Output is [N, T, C] — read T and C from the resolved shape.
    const auto out_shape = self.engine_.context()
                               ->getTensorShape(self.cfg_.output_name.c_str());
    if (out_shape.nbDims < 3) {
        throw TrtException("PaddleOcrRecognizer: '" + self.cfg_.output_name +
                           "' has fewer than 3 dims (expected [N, T, C])");
    }
    self.seq_len_     = static_cast<int>(out_shape.d[1]);
    self.num_classes_ = static_cast<int>(out_shape.d[2]);
    if (self.seq_len_ <= 0 || self.num_classes_ <= 0) {
        throw TrtException("PaddleOcrRecognizer: could not resolve output [T, C]");
    }
    if (self.num_classes_ != static_cast<int>(self.dictionary_.size()) + 1) {
        throw TrtException("PaddleOcrRecognizer: dictionary size " +
                           std::to_string(self.dictionary_.size()) +
                           " + 1 blank does not match model output classes " +
                           std::to_string(self.num_classes_));
    }

    self.input_chw_.assign(static_cast<std::size_t>(3 * self.cfg_.input_height *
                                                    self.cfg_.input_width), 0.0f);
    self.output_logits_.assign(static_cast<std::size_t>(self.seq_len_ * self.num_classes_), 0.0f);

    spdlog::info("PaddleOcrRecognizer ready: input {}x{}, T={}, C={} (dict={})",
                 self.cfg_.input_width, self.cfg_.input_height,
                 self.seq_len_, self.num_classes_, self.dictionary_.size());

    return self;
}

void PaddleOcrRecognizer::preprocess_(const cv::Mat& src, float* dst_chw) const {
    if (src.empty() || src.type() != CV_8UC3) {
        throw TrtException("PaddleOcrRecognizer::preprocess: input must be CV_8UC3 BGR");
    }

    // 1) Aspect-preserving resize to the model's input height. New width is
    //    capped at input_width so very wide crops don't blow up the canvas.
    const float aspect = static_cast<float>(src.cols) / static_cast<float>(src.rows);
    int new_w = static_cast<int>(std::round(cfg_.input_height * aspect));
    new_w     = std::clamp(new_w, 1, cfg_.input_width);

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(new_w, cfg_.input_height), 0, 0, cv::INTER_LINEAR);

    // 2) Right-pad with zeros to input_width. PaddleOCR's CTC head treats the
    //    padded columns as low-energy time steps and decodes them as blanks.
    cv::Mat canvas{cfg_.input_height, cfg_.input_width, CV_8UC3, cv::Scalar(0, 0, 0)};
    resized.copyTo(canvas(cv::Rect(0, 0, new_w, cfg_.input_height)));

    // 3) BGR → float32 normalized: (pixel/255 - mean) / std, per channel.
    //    Done as a single cvtColor + per-channel mul/sub via cv::dnn::blobFromImage
    //    style: we do it manually with split / arithmetic to avoid pulling
    //    opencv_dnn into the link line.
    cv::Mat f32;
    canvas.convertTo(f32, CV_32FC3, 1.0 / 255.0);

    std::vector<cv::Mat> channels(3);
    cv::split(f32, channels);
    // PaddleOCR is trained on RGB-mean normalization but reads BGR at the C++
    // boundary; the mean/std are intentionally symmetric (0.5, 0.5, 0.5) in
    // the default config so channel order is irrelevant. If a custom config
    // breaks that symmetry, the per-channel order here matches OpenCV's BGR.
    for (int c = 0; c < 3; ++c) {
        channels[c] = (channels[c] - cfg_.mean[c]) / cfg_.stddev[c];
    }

    // 4) Pack into the contiguous CHW destination buffer.
    const int plane = cfg_.input_height * cfg_.input_width;
    for (int c = 0; c < 3; ++c) {
        cv::Mat plane_view{cfg_.input_height, cfg_.input_width, CV_32FC1,
                           dst_chw + c * plane};
        channels[c].copyTo(plane_view);
    }
}

RecognizedPlate PaddleOcrRecognizer::ctc_decode_(const float* logits) const {
    std::string text;
    text.reserve(static_cast<std::size_t>(seq_len_));
    double  conf_sum = 0.0;
    int     kept     = 0;
    int     prev_idx = -1;

    for (int t = 0; t < seq_len_; ++t) {
        const float* row = logits + t * num_classes_;

        // argmax along class dim
        int   best_idx  = 0;
        float best_prob = row[0];
        for (int c = 1; c < num_classes_; ++c) {
            if (row[c] > best_prob) {
                best_prob = row[c];
                best_idx  = c;
            }
        }

        // CTC: skip blank (index 0) and skip immediate repeats.
        if (best_idx == 0 || best_idx == prev_idx) {
            prev_idx = best_idx;
            continue;
        }

        // best_idx 1..num_classes_-1 maps to dictionary_[best_idx - 1].
        text     += dictionary_[static_cast<std::size_t>(best_idx - 1)];
        conf_sum += best_prob;
        ++kept;
        prev_idx = best_idx;
    }

    RecognizedPlate r;
    r.text       = std::move(text);
    r.confidence = (kept > 0) ? static_cast<float>(conf_sum / kept) : 0.0f;
    return r;
}

RecognizedPlate PaddleOcrRecognizer::recognize(const cv::Mat& plate_bgr) {
    preprocess_(plate_bgr, input_chw_.data());

    const std::unordered_map<std::string, std::span<const std::byte>> host_in{
        {cfg_.input_name, as_bytes(input_chw_)},
    };
    const std::unordered_map<std::string, std::span<std::byte>> host_out{
        {cfg_.output_name, as_writable_bytes(output_logits_)},
    };
    engine_.enqueue(host_in, host_out);
    engine_.sync();

    return ctc_decode_(output_logits_.data());
}

std::vector<RecognizedPlate>
PaddleOcrRecognizer::recognize_batch(const std::vector<cv::Mat>& plates) {
    std::vector<RecognizedPlate> out;
    out.reserve(plates.size());
    for (const auto& p : plates) {
        out.push_back(recognize(p));
    }
    return out;
}

}  // namespace gate::inference
