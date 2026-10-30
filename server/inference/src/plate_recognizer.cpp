// plate_recognizer.cpp

#include "inference/plate_recognizer.hpp"

#include <fstream>
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>

#include "inference/detail/cpu_algorithms.hpp"
#include "inference/plate_text.hpp"

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
        throw TrtException("PlateRecognizer: cannot open dictionary: " + path.string());
    }
    std::vector<std::string> chars;
    std::string line;
    while (std::getline(f, line)) {
        // One character per line, kept verbatim (a space entry is meaningful);
        // only a Windows CR is dropped.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        chars.push_back(std::move(line));
    }
    if (chars.empty()) {
        throw TrtException("PlateRecognizer: dictionary is empty: " + path.string());
    }
    return chars;
}

PlateModelConfig read_plate_config(const std::filesystem::path& path) {
    std::ifstream f{path};
    if (!f) {
        throw TrtException("PlateRecognizer: cannot open plate config: " + path.string());
    }
    try {
        return parse_plate_config(f);
    } catch (const std::exception& e) {
        throw TrtException(path.string() + ": " + e.what());
    }
}

const char* mode_name(PlateRecognizer::Mode m) {
    return m == PlateRecognizer::Mode::kCtc ? "ctc" : "slots";
}

}  // namespace

PlateRecognizer::PlateRecognizer(TrtEngine&& engine, Config cfg)
    : engine_(std::move(engine)), cfg_(std::move(cfg)) {}

PlateRecognizer PlateRecognizer::load(Config cfg, nvinfer1::ILogger& logger) {
    // Read the text side first: a bad dictionary / config fails before the
    // (slower) engine deserialisation.
    std::vector<std::string> dict;
    PlateModelConfig pcfg;
    if (cfg.mode == Mode::kCtc) {
        dict = read_dictionary(cfg.dictionary_path);
    } else {
        pcfg = read_plate_config(cfg.plate_config_path);
        cfg.input_height = pcfg.img_height;
        cfg.input_width = pcfg.img_width;
        if (pcfg.keep_aspect_ratio || pcfg.image_color_mode != "rgb") {
            throw TrtException(
                "PlateRecognizer: only keep_aspect_ratio=false, image_color_mode=rgb slot "
                "models are supported: " +
                cfg.plate_config_path.string());
        }
    }

    auto engine = TrtEngine::load(cfg.engine_path, logger);
    PlateRecognizer self{std::move(engine), std::move(cfg)};
    self.dictionary_ = std::move(dict);
    self.alphabet_ = pcfg.alphabet;
    self.pad_char_ = pcfg.pad_char;
    auto& c = self.cfg_;

    // ---- Resolve tensor names ----
    std::vector<const TensorIo*> inputs;
    std::vector<const TensorIo*> outputs;
    for (const auto& io : self.engine_.tensors()) {
        (io.is_input ? inputs : outputs).push_back(&io);
    }
    if (inputs.size() != 1 || outputs.empty()) {
        throw TrtException("PlateRecognizer: expected one input and at least one output");
    }
    if (c.input_name.empty()) {
        c.input_name = inputs.front()->name;
    }
    if (c.output_name.empty()) {
        c.output_name = outputs.front()->name;
        for (const auto* o : outputs) {
            if (c.mode == Mode::kSlots && o->name == "plate") {
                c.output_name = o->name;
            }
        }
    }
    const auto& in = self.engine_.tensor(c.input_name);
    if (in.shape.nbDims != 4) {
        throw TrtException("PlateRecognizer: input '" + c.input_name + "' must be 4-D");
    }

    // ---- Input layout and size ----
    // kCtc: [N, 3, H, W] float. kSlots: [N, H, W, 3] uint8.
    const bool nhwc = c.mode == Mode::kSlots;
    const auto h_dim = in.shape.d[nhwc ? 1 : 2];
    const auto w_dim = in.shape.d[nhwc ? 2 : 3];
    if (c.input_height <= 0) {
        c.input_height = static_cast<int>(h_dim);
    }
    if (c.input_width <= 0) {
        c.input_width = static_cast<int>(w_dim);
    }
    if (c.input_height <= 0 || c.input_width <= 0) {
        throw TrtException("PlateRecognizer: input size is dynamic; set input_width/height");
    }
    if ((h_dim > 0 && h_dim != c.input_height) || (w_dim > 0 && w_dim != c.input_width)) {
        throw TrtException("PlateRecognizer: configured input " + std::to_string(c.input_width) +
                           "x" + std::to_string(c.input_height) + " does not match engine input " +
                           shape_to_string(in.shape));
    }
    const auto want_type = nhwc ? nvinfer1::DataType::kUINT8 : nvinfer1::DataType::kFLOAT;
    if (in.dtype != want_type) {
        throw TrtException(std::string{"PlateRecognizer: mode '"} + mode_name(c.mode) +
                           "' needs a " + (nhwc ? "uint8 NHWC" : "float32 NCHW") + " input, '" +
                           c.input_name + "' is not");
    }
    const nvinfer1::Dims4 dims = nhwc ? nvinfer1::Dims4{1, c.input_height, c.input_width, 3}
                                      : nvinfer1::Dims4{1, 3, c.input_height, c.input_width};
    self.engine_.set_input_shape(c.input_name, dims);

    // ---- Output [N, T|S, C] ----
    const auto out = self.engine_.context()->getTensorShape(c.output_name.c_str());
    if (out.nbDims != 3 || out.d[1] <= 0 || out.d[2] <= 0) {
        throw TrtException("PlateRecognizer: output '" + c.output_name +
                           "' must be [N, T, C], is " + shape_to_string(out));
    }
    self.steps_ = static_cast<int>(out.d[1]);
    self.num_classes_ = static_cast<int>(out.d[2]);
    if (c.mode == Mode::kCtc &&
        self.num_classes_ != static_cast<int>(self.dictionary_.size()) + 1) {
        throw TrtException(
            "PlateRecognizer: dictionary size " + std::to_string(self.dictionary_.size()) +
            " + 1 blank does not match model output classes " + std::to_string(self.num_classes_));
    }
    if (c.mode == Mode::kSlots && self.num_classes_ != static_cast<int>(self.alphabet_.size())) {
        throw TrtException(
            "PlateRecognizer: alphabet size " + std::to_string(self.alphabet_.size()) +
            " does not match model output classes " + std::to_string(self.num_classes_));
    }

    const auto px = static_cast<std::size_t>(3) * static_cast<std::size_t>(c.input_height) *
                    static_cast<std::size_t>(c.input_width);
    if (nhwc) {
        self.input_u8_.assign(px, 0);
    } else {
        self.input_f32_.assign(px, 0.0f);
    }
    self.output_.assign(static_cast<std::size_t>(self.steps_) * self.num_classes_, 0.0f);

    spdlog::info("PlateRecognizer ready: mode={}, input '{}' {}x{}, output '{}' [{} x {}]",
                 mode_name(c.mode), c.input_name, c.input_width, c.input_height, c.output_name,
                 self.steps_, self.num_classes_);
    return self;
}

RecognizedPlate PlateRecognizer::recognize(const cv::Mat& plate_bgr) {
    if (plate_bgr.empty() || plate_bgr.type() != CV_8UC3) {
        throw TrtException(
            "PlateRecognizer::recognize: input must be a non-empty CV_8UC3 BGR crop");
    }
    std::span<const std::byte> in_bytes;
    if (cfg_.mode == Mode::kSlots) {
        detail::preprocess_stretch_rgb_u8(plate_bgr, cfg_.input_height, cfg_.input_width,
                                          input_u8_.data());
        in_bytes = as_bytes(input_u8_);
    } else {
        detail::preprocess_ctc_keep_aspect(plate_bgr, cfg_.input_height, cfg_.input_width,
                                           cfg_.mean, cfg_.stddev, input_f32_.data());
        in_bytes = as_bytes(input_f32_);
    }

    const std::unordered_map<std::string, std::span<const std::byte>> host_in{
        {cfg_.input_name, in_bytes},
    };
    const std::unordered_map<std::string, std::span<std::byte>> host_out{
        {cfg_.output_name, as_writable_bytes(output_)},
    };
    engine_.enqueue(host_in, host_out);
    engine_.sync();

    if (cfg_.mode == Mode::kSlots) {
        auto d = slot_decode(output_, steps_, num_classes_, alphabet_, pad_char_);
        return {std::move(d.text), d.mean_confidence};
    }
    auto d = detail::ctc_greedy_decode(output_, steps_, num_classes_, dictionary_);
    return {std::move(d.text), d.mean_confidence};
}

std::vector<RecognizedPlate> PlateRecognizer::recognize_batch(const std::vector<cv::Mat>& plates) {
    std::vector<RecognizedPlate> out;
    out.reserve(plates.size());
    for (const auto& p : plates) {
        out.push_back(recognize(p));
    }
    return out;
}

}  // namespace gate::inference
