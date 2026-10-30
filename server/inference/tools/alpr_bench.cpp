// alpr_bench.cpp — run the production AlprPipeline over a list of images.
//
// Offline evaluation / parity harness for the TensorRT engines (no camera,
// no gRPC). For every image it writes one JSON line with
//   * "dets":     the detector's boxes and scores (YoloPlateDetector::detect,
//                 confidence floor from --detector-conf-floor)
//   * "readings": AlprPipeline::process — box, raw and Kenya-normalised text,
//                 detector and OCR scores
//   * "det_ms", "frame_ms": wall time of detect() and of process()
// scripts/export-models/eval/cpp_parity.py scores the output against the
// Kenya test set and against ONNX Runtime on the original ONNX models.
//
//   alpr_bench --detector-engine yolov9_plate.plan --recognizer-engine plate_ocr.plan
//              --plate-config plate_ocr_config.yaml --images list.txt --out out.jsonl
//
// list.txt: one "<id> <image path>" per line.

#include <CLI/CLI.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <opencv2/imgcodecs.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <vector>

#include "inference/alpr_pipeline.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Plate strings are [A-Z0-9]; ids are file stems. Escape the JSON specials
// anyway so a stray character cannot break a line.
std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
            out += buf;
        } else {
            out.push_back(c);
        }
    }
    return out + "\"";
}

std::string box_json(const cv::Rect2f& b) {
    std::ostringstream o;
    o.precision(6);
    o << '[' << b.x << ',' << b.y << ',' << (b.x + b.width) << ',' << (b.y + b.height) << ']';
    return o.str();
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"alpr_bench: AlprPipeline over an image list, JSON lines out"};
    gate::inference::AlprPipeline::Config cfg;
    cfg.recognizer.mode = gate::inference::PlateRecognizer::Mode::kSlots;
    std::string images;
    std::string out_path;
    int warmup = 10;
    std::string dump_dir;
    std::vector<std::string> dump_ids;

    app.add_option("--detector-engine", cfg.detector.engine_path)->required();
    app.add_option("--detector-conf-floor", cfg.detector.confidence_floor)->capture_default_str();
    app.add_option("--recognizer-engine", cfg.recognizer.engine_path)->required();
    app.add_option("--recognizer-mode", cfg.recognizer.mode)
        ->transform(CLI::CheckedTransformer(
            std::map<std::string, gate::inference::PlateRecognizer::Mode>{
                {"slots", gate::inference::PlateRecognizer::Mode::kSlots},
                {"ctc", gate::inference::PlateRecognizer::Mode::kCtc}},
            CLI::ignore_case))
        ->default_str("slots");
    app.add_option("--plate-config", cfg.recognizer.plate_config_path);
    app.add_option("--ocr-dictionary", cfg.recognizer.dictionary_path);
    app.add_option("--images", images, "List file: '<id> <path>' per line")->required();
    app.add_option("--out", out_path, "JSON-lines output")->required();
    app.add_option("--warmup", warmup, "Untimed passes over the first image")
        ->capture_default_str();
    app.add_option("--dump-dir", dump_dir,
                   "Write each --dump-ids image's detector input tensor here as <id>.f32 "
                   "(raw float32, 1x3xHxW)");
    app.add_option("--dump-ids", dump_ids, "Image ids to dump (default: none)")->delimiter(',');
    CLI11_PARSE(app, argc, argv);

    std::vector<std::pair<std::string, std::string>> list;
    {
        std::ifstream in{images};
        if (!in) {
            spdlog::error("cannot read {}", images);
            return 1;
        }
        std::string id, path;
        while (in >> id >> path) {
            list.emplace_back(id, path);
        }
    }
    if (list.empty()) {
        spdlog::error("no images in {}", images);
        return 1;
    }

    try {
        gate::inference::TrtLogger logger;
        auto pipeline = gate::inference::AlprPipeline::load(cfg, logger);
        std::ofstream out{out_path};

        const cv::Mat first = cv::imread(list.front().second, cv::IMREAD_COLOR);
        for (int i = 0; i < warmup && !first.empty(); ++i) {
            (void)pipeline.process(first);
        }

        for (const auto& [id, path] : list) {
            const cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
            if (img.empty()) {
                spdlog::error("{}: cannot read {}", id, path);
                return 1;
            }
            auto t0 = Clock::now();
            const auto dets = pipeline.detector().detect(img);
            const double det_ms = ms_since(t0);
            if (!dump_dir.empty() &&
                std::find(dump_ids.begin(), dump_ids.end(), id) != dump_ids.end()) {
                const auto t = pipeline.detector().last_input();
                std::ofstream f{std::filesystem::path{dump_dir} / (id + ".f32"), std::ios::binary};
                f.write(reinterpret_cast<const char*>(t.data()),
                        static_cast<std::streamsize>(t.size_bytes()));
            }
            t0 = Clock::now();
            const auto readings = pipeline.process(img);
            const double frame_ms = ms_since(t0);

            out << "{\"id\":" << json_str(id) << ",\"det_ms\":" << det_ms
                << ",\"frame_ms\":" << frame_ms << ",\"dets\":[";
            for (std::size_t i = 0; i < dets.size(); ++i) {
                out << (i ? "," : "") << "{\"box\":" << box_json(dets[i].box)
                    << ",\"score\":" << dets[i].confidence << '}';
            }
            out << "],\"readings\":[";
            for (std::size_t i = 0; i < readings.size(); ++i) {
                const auto& r = readings[i];
                out << (i ? "," : "") << "{\"box\":" << box_json(r.box)
                    << ",\"raw\":" << json_str(r.raw_text) << ",\"text\":" << json_str(r.text)
                    << ",\"kenya\":" << (r.kenya_format ? "true" : "false")
                    << ",\"det_score\":" << r.detection_score << ",\"ocr_score\":" << r.ocr_score
                    << '}';
            }
            out << "]}\n";
        }
        spdlog::info("wrote {} lines to {}", list.size(), out_path);
    } catch (const std::exception& e) {
        spdlog::error("alpr_bench: {}", e.what());
        return 1;
    }
    return 0;
}
