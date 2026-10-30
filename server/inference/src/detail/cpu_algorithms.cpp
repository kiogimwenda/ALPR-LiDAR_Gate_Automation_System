// detail/cpu_algorithms.cpp

#include "inference/detail/cpu_algorithms.hpp"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace gate::inference::detail {

LetterboxParams letterbox(const cv::Mat& src, int dst_w, int dst_h, cv::Mat& dst, int pad_value) {
    const float src_w = static_cast<float>(src.cols);
    const float src_h = static_cast<float>(src.rows);
    const float dw = static_cast<float>(dst_w);
    const float dh = static_cast<float>(dst_h);

    const float scale = std::min(dw / src_w, dh / src_h);
    const int new_w = static_cast<int>(std::round(src_w * scale));
    const int new_h = static_cast<int>(std::round(src_h * scale));

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

    const int pad_x = (dst_w - new_w) / 2;
    const int pad_y = (dst_h - new_h) / 2;
    dst.create(dst_h, dst_w, src.type());
    dst.setTo(cv::Scalar::all(pad_value));
    resized.copyTo(dst(cv::Rect(pad_x, pad_y, new_w, new_h)));

    return {scale, static_cast<float>(pad_x), static_cast<float>(pad_y)};
}

cv::Rect2f unmap_letterbox_box(float x1_canvas, float y1_canvas, float x2_canvas, float y2_canvas,
                               const LetterboxParams& lb, int src_w, int src_h) {
    const float x1 = std::max(0.0f, (x1_canvas - lb.pad_x) / lb.scale);
    const float y1 = std::max(0.0f, (y1_canvas - lb.pad_y) / lb.scale);
    const float x2 = std::min(static_cast<float>(src_w), (x2_canvas - lb.pad_x) / lb.scale);
    const float y2 = std::min(static_cast<float>(src_h), (y2_canvas - lb.pad_y) / lb.scale);
    if (x2 <= x1 || y2 <= y1) {
        return cv::Rect2f{};
    }
    return cv::Rect2f(x1, y1, x2 - x1, y2 - y1);
}

CtcResult ctc_greedy_decode(std::span<const float> logits, int seq_len, int num_classes,
                            const std::vector<std::string>& dictionary) {
    CtcResult r{};
    if (seq_len <= 0 || num_classes <= 0) {
        return r;
    }

    r.text.reserve(static_cast<std::size_t>(seq_len));
    double conf_sum = 0.0;
    int kept = 0;
    int prev_idx = -1;

    for (int t = 0; t < seq_len; ++t) {
        const float* row = logits.data() + static_cast<std::size_t>(t) * num_classes;

        int best_idx = 0;
        float best_prob = row[0];
        for (int c = 1; c < num_classes; ++c) {
            if (row[c] > best_prob) {
                best_prob = row[c];
                best_idx = c;
            }
        }

        if (best_idx == 0 || best_idx == prev_idx) {
            prev_idx = best_idx;
            continue;
        }

        const std::size_t dict_pos = static_cast<std::size_t>(best_idx - 1);
        if (dict_pos < dictionary.size()) {
            r.text += dictionary[dict_pos];
        }
        conf_sum += best_prob;
        ++kept;
        prev_idx = best_idx;
    }

    r.mean_confidence = (kept > 0) ? static_cast<float>(conf_sum / kept) : 0.0f;
    return r;
}

cv::Rect expand_box_with_margin(const cv::Rect2f& box, const cv::Size& frame_size, float margin,
                                int min_extent) {
    const float dx = box.width * margin;
    const float dy = box.height * margin;

    float x1 = std::clamp(box.x - dx, 0.0f, static_cast<float>(frame_size.width));
    float y1 = std::clamp(box.y - dy, 0.0f, static_cast<float>(frame_size.height));
    float x2 = std::clamp(box.x + box.width + dx, 0.0f, static_cast<float>(frame_size.width));
    float y2 = std::clamp(box.y + box.height + dy, 0.0f, static_cast<float>(frame_size.height));

    const int xi = static_cast<int>(std::floor(x1));
    const int yi = static_cast<int>(std::floor(y1));
    const int wi = static_cast<int>(std::ceil(x2 - x1));
    const int hi = static_cast<int>(std::ceil(y2 - y1));

    if (wi < min_extent || hi < min_extent) {
        return cv::Rect{};
    }
    return cv::Rect{xi, yi, wi, hi};
}

int preprocess_ctc_keep_aspect(const cv::Mat& bgr, int dst_h, int dst_w,
                               const std::array<float, 3>& mean, const std::array<float, 3>& stddev,
                               float* dst_chw) {
    const double aspect = static_cast<double>(bgr.cols) / static_cast<double>(bgr.rows);
    const int new_w = std::clamp(static_cast<int>(std::round(dst_h * aspect)), 1, dst_w);

    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(new_w, dst_h), 0, 0, cv::INTER_LINEAR);

    const std::size_t plane = static_cast<std::size_t>(dst_h) * static_cast<std::size_t>(dst_w);
    std::fill(dst_chw, dst_chw + 3 * plane, 0.0f);
    for (int y = 0; y < dst_h; ++y) {
        const auto* row = resized.ptr<cv::Vec3b>(y);
        for (int x = 0; x < new_w; ++x) {
            for (int c = 0; c < 3; ++c) {
                const float v = static_cast<float>(row[x][c]) / 255.0f;
                dst_chw[static_cast<std::size_t>(c) * plane +
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(dst_w) +
                        static_cast<std::size_t>(x)] = (v - mean[c]) / stddev[c];
            }
        }
    }
    return new_w;
}

void preprocess_stretch_rgb_u8(const cv::Mat& bgr, int dst_h, int dst_w, std::uint8_t* dst_hwc) {
    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(dst_w, dst_h), 0, 0, cv::INTER_LINEAR);
    cv::Mat rgb(dst_h, dst_w, CV_8UC3, dst_hwc);  // writes straight into the caller's buffer
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
}

}  // namespace gate::inference::detail
