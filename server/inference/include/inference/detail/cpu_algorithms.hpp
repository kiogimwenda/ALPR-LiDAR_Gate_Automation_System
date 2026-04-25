// detail/cpu_algorithms.hpp
//
// Pure-CPU inference algorithms — letterbox geometry, CTC greedy decode, crop
// expansion. These are the host-side bits of the YoloPlateDetector,
// PaddleOcrRecognizer, and AlprPipeline classes lifted out of their methods so
// they can be unit-tested directly without booting TensorRT.
//
// Nothing here depends on libnvinfer; only OpenCV. Tests link against an
// inference-cpu-only static lib so they run wherever OpenCV does.
#pragma once

#include <opencv2/core.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gate::inference::detail {

// Result of an aspect-preserving letterbox resize. The caller uses these to
// remap detection boxes from input-canvas pixels back to source-image pixels.
struct LetterboxParams {
    float scale;  // src px → dst px; same factor for both axes
    float pad_x;  // pixels of padding inserted on the LEFT of the canvas
    float pad_y;  // pixels of padding inserted on the TOP of the canvas
};

// Resize `src` into a (dst_w x dst_h) canvas preserving aspect ratio. Free
// space is filled with `pad_value` on every channel. The input must be 8-bit
// 3-channel; matches what the YOLO preprocess path consumes from cameras.
LetterboxParams letterbox(const cv::Mat& src, int dst_w, int dst_h, cv::Mat& dst,
                          int pad_value = 114);

// Inverse of letterbox: take a box in canvas-pixel space and return the
// equivalent rectangle in the source image's pixel space, clamped to the
// source dimensions. A box that clamps to a degenerate rect (zero area) is
// returned as `cv::Rect2f{}` so the caller can drop it cheaply.
cv::Rect2f unmap_letterbox_box(float x1_canvas, float y1_canvas, float x2_canvas, float y2_canvas,
                               const LetterboxParams& lb, int src_w, int src_h);

// Result of a CTC greedy decode pass.
struct CtcResult {
    std::string text;
    float mean_confidence;  // mean of the argmax probs of the kept characters
};

// Greedy CTC decode on a `[T, C]` post-softmax probability tensor. Class 0 is
// the blank token; classes 1..C-1 map to `dictionary[c-1]`. Repeated
// consecutive argmaxes collapse to one character (standard CTC). Returns the
// decoded text and the mean argmax-probability across the *kept* characters
// (zero if every step was a blank or repeat).
CtcResult ctc_greedy_decode(std::span<const float> logits, int seq_len, int num_classes,
                            const std::vector<std::string>& dictionary);

// Expand a box outward by `margin` * (width, height) on each side and clamp
// to the frame. The result is integer-pixel and pixel-inclusive on both axes,
// safe to use as a `cv::Mat(roi)` view. Returns a 0×0 rect if the expansion
// produces a degenerate ROI (e.g. width or height < `min_extent`).
cv::Rect expand_box_with_margin(const cv::Rect2f& box, const cv::Size& frame_size, float margin,
                                int min_extent = 4);

}  // namespace gate::inference::detail
