// cpu_algorithms_test.cpp — unit tests for the host-side inference algorithms.
//
// These cover the CPU-only paths of the inference layer: letterbox geometry,
// CTC greedy decode, and crop-with-margin. They do not exercise TensorRT;
// integration tests with a real engine are a Phase 4.9 concern.

#include "inference/detail/cpu_algorithms.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <opencv2/core.hpp>
#include <vector>

using gate::inference::detail::ctc_greedy_decode;
using gate::inference::detail::expand_box_with_margin;
using gate::inference::detail::letterbox;
using gate::inference::detail::LetterboxParams;
using gate::inference::detail::unmap_letterbox_box;

using Catch::Matchers::WithinAbs;

namespace {

// Build a single-row argmax probability tensor: at every time step, class
// `class_seq[t]` is the argmax with probability `peak`, all other classes
// share `(1 - peak) / (C - 1)` so the rows sum to 1 and the argmax is
// unambiguous.
std::vector<float> make_argmax_logits(const std::vector<int>& class_seq, int num_classes,
                                      float peak) {
    const int T = static_cast<int>(class_seq.size());
    std::vector<float> logits(static_cast<std::size_t>(T) * num_classes, 0.0f);
    const float low = (1.0f - peak) / (num_classes - 1);
    for (int t = 0; t < T; ++t) {
        for (int c = 0; c < num_classes; ++c) {
            logits[t * num_classes + c] = low;
        }
        logits[t * num_classes + class_seq[t]] = peak;
    }
    return logits;
}

}  // namespace

TEST_CASE("letterbox: horizontal source pads top and bottom", "[letterbox]") {
    cv::Mat src(100, 200, CV_8UC3, cv::Scalar(50, 50, 50));  // 200x100
    cv::Mat dst;
    const auto p = letterbox(src, 320, 320, dst, 114);

    REQUIRE(dst.cols == 320);
    REQUIRE(dst.rows == 320);
    // Source is 2:1; canvas is 1:1. Scale picks the smaller axis: 320/200=1.6.
    REQUIRE_THAT(p.scale, WithinAbs(1.6f, 1e-5f));
    // Resized image fits 320 wide × 160 tall, so 80 px padding above and below.
    REQUIRE_THAT(p.pad_x, WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(p.pad_y, WithinAbs(80.0f, 1e-5f));
    // Padding pixel is the neutral gray we asked for.
    REQUIRE(dst.at<cv::Vec3b>(5, 160) == cv::Vec3b(114, 114, 114));
    // The image content survives in the centered band.
    REQUIRE(dst.at<cv::Vec3b>(160, 160) == cv::Vec3b(50, 50, 50));
}

TEST_CASE("letterbox: vertical source pads left and right", "[letterbox]") {
    cv::Mat src(200, 100, CV_8UC3, cv::Scalar(0, 255, 0));  // 100x200
    cv::Mat dst;
    const auto p = letterbox(src, 320, 320, dst);

    // Scale picks min(320/100, 320/200) = 1.6. Resized → 160×320.
    REQUIRE_THAT(p.scale, WithinAbs(1.6f, 1e-5f));
    REQUIRE_THAT(p.pad_x, WithinAbs(80.0f, 1e-5f));
    REQUIRE_THAT(p.pad_y, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("unmap_letterbox_box: center box round-trips through scale and pad", "[letterbox]") {
    // Source 200x100 → canvas 320x320; scale=1.6, pad_y=80, pad_x=0.
    LetterboxParams lb{1.6f, 0.0f, 80.0f};

    // A box at canvas (40, 120, 280, 200) should map back to source
    // (40/1.6, (120-80)/1.6, 280/1.6, (200-80)/1.6) = (25, 25, 175, 75).
    const auto out = unmap_letterbox_box(40, 120, 280, 200, lb, 200, 100);
    REQUIRE_THAT(out.x, WithinAbs(25.0f, 1e-4f));
    REQUIRE_THAT(out.y, WithinAbs(25.0f, 1e-4f));
    REQUIRE_THAT(out.width, WithinAbs(150.0f, 1e-4f));
    REQUIRE_THAT(out.height, WithinAbs(50.0f, 1e-4f));
}

TEST_CASE("unmap_letterbox_box: clamps to source extents and drops degenerate", "[letterbox]") {
    LetterboxParams lb{1.6f, 0.0f, 80.0f};

    // A box that extends below the canvas's image band (y > 240) clamps to
    // the source bottom (100).
    const auto clamped = unmap_letterbox_box(0, 80, 320, 320, lb, 200, 100);
    REQUIRE_THAT(clamped.x, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(clamped.y, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(clamped.width, WithinAbs(200.0f, 1e-4f));
    REQUIRE_THAT(clamped.height, WithinAbs(100.0f, 1e-4f));

    // A box entirely inside the top padding strip is degenerate after clamp.
    const auto degenerate = unmap_letterbox_box(10, 0, 50, 40, lb, 200, 100);
    REQUIRE(degenerate.width == 0.0f);
    REQUIRE(degenerate.height == 0.0f);
}

TEST_CASE("ctc_greedy_decode: skips blanks and collapses repeats", "[ctc]") {
    // Dictionary: A B C → indices 1 2 3 (0 is blank).
    std::vector<std::string> dict = {"A", "B", "C"};
    const int C = 4;  // 3 chars + blank

    // Sequence: blank, A, A, blank, B, B, C, blank → "ABC"
    auto logits = make_argmax_logits({0, 1, 1, 0, 2, 2, 3, 0}, C, 0.9f);

    const auto r = ctc_greedy_decode(std::span<const float>{logits}, 8, C, dict);
    REQUIRE(r.text == "ABC");
    // 3 non-blank, non-repeat steps were kept; each had probability 0.9.
    REQUIRE_THAT(r.mean_confidence, WithinAbs(0.9f, 1e-5f));
}

TEST_CASE("ctc_greedy_decode: all blanks produces empty text and zero confidence", "[ctc]") {
    std::vector<std::string> dict = {"A", "B"};
    auto logits = make_argmax_logits({0, 0, 0, 0}, 3, 0.95f);

    const auto r = ctc_greedy_decode(std::span<const float>{logits}, 4, 3, dict);
    REQUIRE(r.text.empty());
    REQUIRE(r.mean_confidence == 0.0f);
}

TEST_CASE("ctc_greedy_decode: confidence is mean of kept probs, not sum", "[ctc]") {
    // 3 unique chars with peak probability 0.5 (rest split evenly).
    std::vector<std::string> dict = {"X", "Y", "Z"};
    auto logits = make_argmax_logits({1, 2, 3}, 4, 0.5f);

    const auto r = ctc_greedy_decode(std::span<const float>{logits}, 3, 4, dict);
    REQUIRE(r.text == "XYZ");
    REQUIRE_THAT(r.mean_confidence, WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("ctc_greedy_decode: out-of-range class indices are silently dropped", "[ctc]") {
    // Dictionary has only 2 entries but we feed a class index beyond it.
    std::vector<std::string> dict = {"A", "B"};
    // Manually craft a logits row whose argmax is class 5 (no dictionary slot).
    std::vector<float> logits(1 * 6, 0.0f);
    logits[5] = 0.99f;  // class 5
    const auto r = ctc_greedy_decode(std::span<const float>{logits}, 1, 6, dict);
    // The kept-count still increments (the algorithm doesn't know the
    // dictionary is short) but the text doesn't grow.
    REQUIRE(r.text.empty());
    REQUIRE_THAT(r.mean_confidence, WithinAbs(0.99f, 1e-5f));
}

TEST_CASE("expand_box_with_margin: 10% margin grows symmetrically", "[crop]") {
    cv::Rect2f box{100, 100, 100, 50};
    const auto out = expand_box_with_margin(box, cv::Size{640, 480}, 0.1f);
    // Expanded: x=[100-10, 200+10]=[90,210]; y=[100-5, 150+5]=[95,155].
    REQUIRE(out.x == 90);
    REQUIRE(out.y == 95);
    REQUIRE(out.width == 120);
    REQUIRE(out.height == 60);
}

TEST_CASE("expand_box_with_margin: clamps to frame on every edge", "[crop]") {
    // Box at the top-left corner — any expansion must clamp to (0,0).
    cv::Rect2f box{2, 2, 30, 20};
    const auto out = expand_box_with_margin(box, cv::Size{640, 480}, 0.5f);
    REQUIRE(out.x == 0);
    REQUIRE(out.y == 0);
    REQUIRE(out.x + out.width <= 640);
    REQUIRE(out.y + out.height <= 480);
}

TEST_CASE("expand_box_with_margin: degenerate result is reported as empty", "[crop]") {
    // 2x2 box with min_extent=4 cannot survive even with zero margin.
    cv::Rect2f tiny{10, 10, 2, 2};
    const auto out = expand_box_with_margin(tiny, cv::Size{640, 480}, 0.0f, /*min_extent=*/4);
    REQUIRE(out.width == 0);
    REQUIRE(out.height == 0);
}
