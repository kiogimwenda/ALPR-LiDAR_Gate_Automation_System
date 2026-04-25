# ALPR + LiDAR Automated Vehicle Gate Control System

A production-grade automated vehicle gate controller that fuses **Automatic
License Plate Recognition (ALPR)** with **3D LiDAR** vehicle classification
to authorize or deny entry. ALPR runs YOLOv9 for plate detection and
PaddleOCR for character recognition, both accelerated via TensorRT on
NVIDIA GPUs. A 3D LiDAR pipeline classifies the vehicle independently. A
custom 4-layer ESP32-S3 field PCB drives the gate motor and senses
limit/safety inputs. A Drogon + SvelteKit dashboard provides real-time
monitoring, manual override, and audit logs.

**Repository:** [github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System](https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System)
**License:** [GPL-3.0](LICENSE) — rationale in [ADR-000](docs/decisions/ADR-000-license.md)

---

## Why this project exists

Residential and small-commercial gate access in Kenya (and most emerging
markets) is currently solved by either a guard with a clipboard or a remote
clicker. Both fail at the same things: tailgating, lost remotes, unrecorded
entries, and zero auditability. This project is a self-hosted, on-prem
alternative that:

- **Recognizes plates from real cameras at the gate** (Hikvision 4 MP PoE),
  not curated benchmarks.
- **Cross-checks plate against vehicle class** via 3D LiDAR — a sedan with
  a truck's plate is denied even if OCR succeeds. This is the LiDAR-fusion
  story that makes the system tailgating- and clone-resistant.
- **Runs entirely on a single GPU server on the local LAN** (RTX 4060 8 GB)
  — no cloud round-trip, no monthly per-gate fees.
- **Is repairable in the field** — every component is sourced from
  Luthuli Avenue (Nairobi) or AliExpress, the BOM is published, and the
  PCB is hand-solderable with documented through-hole points for the
  high-current paths.

---

## High-level architecture

```
┌─────────────────────────── AT THE GATE ──────────────────────────────────┐
│                                                                          │
│   Hikvision 4MP camera ──┐                                              │
│   Unitree L1 LiDAR  ─────┤  PoE switch ──── Ethernet ──── GPU server   │
│   ESP32-S3 field PCB ────┤      │                          │             │
│   Limit switches × 2 ────┘      │                          │             │
│   Photoelectric beam            │                          │             │
│                                                            │             │
│        Relay outputs to gate motor (CENTURION D5/R5)       │             │
│                                                            │             │
└────────────────────────────────────────────────────────────┼─────────────┘
                                                             │
┌─────────────────────────── SERVER ROOM ─────────────────────┼────────────┐
│                                                            │             │
│   GPU server: ALPR (YOLOv9 + PaddleOCR, TensorRT) +       │             │
│   LiDAR pipeline + fusion engine + gRPC server +          │             │
│   Drogon dashboard backend + SvelteKit dashboard frontend │             │
│                                                            │             │
└────────────────────────────────────────────────────────────┴─────────────┘
```

Full subsystem flowcharts: [docs/diagrams/](docs/diagrams/) (7 Mermaid files).

---

## Development timeline

The project executes in five sequential phases. Each phase has a tagged
release on GitHub.

| Phase | Title | Status | Highlights |
|---|---|---|---|
| **0** | Environment bootstrap | ✅ Complete | WSL2 Debian + CUDA 13.1 + cuDNN 9.19 + TensorRT 10.15 + OpenCV 4.14 (CUDA source-build) verified by `smoke_test.cu`. |
| **1** | Clarifications & decisions | ✅ Complete | All 12 design questions answered; defaults accepted; gate type override applied. |
| **2** | Architecture | ✅ Complete | 7 Mermaid flowcharts, 11 ADRs (ADR-000…ADR-010), full directory skeleton, CI workflows. |
| **3** | Hardware research & build guides | ✅ Complete | 9 component guides + master build book + complete BOM + 48-page KiCad 9.0 PCB design guide (PDF). |
| **4** | Implementation | 🔵 **In progress** — see below | Modern C++20 server, ESP-IDF firmware, simulation, dashboard. |
| 5 | Delivery | ⏳ Pending | Runbook, commissioning checklist, demo script, public release. |

### Phase 4 sub-milestones (current)

| # | Milestone | Status |
|---|---|---|
| 4.1 | gRPC wire contract (`shared/proto`) | ✅ Complete |
| **4.2** | **Server inference (TensorRT engines for YOLOv9 + PaddleOCR)** | 🔵 **In progress — see "Latest accomplishment" below** |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.1 | TrtEngine RAII wrapper around TensorRT 10.x | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.2 | YOLOv9 plate detector | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.3 | PaddleOCR character recognizer | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.4 | ALPR pipeline orchestrator | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.5 | Python ONNX → TensorRT conversion tooling | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.6 | Catch2 inference unit tests | ⏳ Next |
| 4.3 | Server RPC + fusion engine | ⏳ Pending |
| 4.4 | Firmware drivers (W5500, relays, sensors) | ⏳ Pending |
| 4.5 | Firmware app (state machine, gRPC client, OTA) | ⏳ Pending |
| 4.6 | Simulation harness | ⏳ Pending |
| 4.7 | Dashboard backend + frontend | ⏳ Pending |
| 4.8 | Deployment scripts (systemd, install) | ⏳ Pending |
| 4.9 | End-to-end integration tests | ⏳ Pending |

---

## Latest accomplishment — Phase 4.2.5: ONNX → TensorRT engine tooling

> **Completed 2026-04-25.** Scripts in
> [`scripts/export-models/`](scripts/export-models/) — `convert_onnx_to_trt.py`,
> `inspect_engine.py`, plus a [README](scripts/export-models/README.md)
> documenting the upstream YOLOv9 / PaddleOCR export commands.

### What I built

Phase 4.2.1–4.2.4 produced a C++ inference layer that consumes serialized
TensorRT engines (`.plan` files). 4.2.5 is the missing piece on the
**Python side**: how those `.plan` files are produced from upstream ONNX
exports, with the dynamic shape profiles, FP16 flags, and hardware
compatibility settings the C++ side expects to find at runtime.

| Artifact | Purpose |
|---|---|
| `scripts/export-models/convert_onnx_to_trt.py` | ONNX → `.plan` builder using TensorRT's Python `Builder` / `OnnxParser` / `BuilderConfig` API. FP16 default, optional INT8, `--min/--opt/--max-shape` profile knobs, `--hardware-compat ampere_plus` default for sm_80…sm_120 portability. Prints final I/O signatures after build. |
| `scripts/export-models/inspect_engine.py` | Deserializes a `.plan` and prints every I/O tensor's name, shape, dtype, and per-profile shape ranges. The fast way to verify the C++ Config tensor names still match what the engine actually exposes. |
| `scripts/export-models/README.md` | Documents the upstream export commands for YOLOv9 (`export.py --include onnx_end2end`) and PaddleOCR (`paddle2onnx`), plus the exact `convert_onnx_to_trt.py` invocations that match the C++ Config defaults. |

### Technical detail

#### Why a thin wrapper instead of `trtexec`

NVIDIA ships `trtexec` for one-off engine builds, and it's fine for that.
The trouble is that the engines this project produces aren't one-off:

- The PP-OCRv4 recognizer needs a **dynamic-shape optimization profile**
  with three width values (`min=32, opt=160, max=320`) chosen so the
  builder picks kernels tuned for typical Kenya plates (≈ 1:3.3 aspect)
  rather than for the worst-case 320 px. Encoding that profile in a
  shell-script `trtexec` invocation is doable but unreadable.
- Both engines need `HardwareCompatibilityLevel.AMPERE_PLUS` so the same
  `.plan` runs on the dev box (RTX 5060, sm_120) and the production
  server (RTX 4060, sm_89). `trtexec` exposes the flag but `--help` is
  ~400 lines and the right combination is not obvious.
- The C++ side binds tensors **by name**. Verifying that `softmax_2.tmp_0`
  is still what PaddleOCR exports needs an inspection step that's not a
  shell pipeline of `trtexec --dumpProfile` parsing.

A small, readable Python script that calls the same TRT API the C++ side
uses internally is easier to maintain than a shell wrapper around an
opaque tool.

#### `convert_onnx_to_trt.py` — what it actually does

Six steps:

1. **Logger.** A `trt.Logger` at `WARNING` (or `INFO` with `-v`) so the
   build output is small enough to read but loud enough to surface ONNX
   parse errors.
2. **Parse.** `Builder.create_network(0)` (explicit batch is now the only
   mode in TRT 10) → `OnnxParser`. On parse failure every parser error
   is dumped before exit so an upstream change to the ONNX surface
   doesn't fail silently.
3. **Builder config.** `set_memory_pool_limit(WORKSPACE, 4 GiB)` (CLI
   override available) — the workspace pool is what the builder uses to
   try alternative kernels, so a stingy budget produces measurably worse
   engines. 4 GiB fits both 8 GB GPUs with room to spare.
4. **Precision flags.** `BuilderFlag.FP16` by default. `INT8` is
   reachable via `--int8` but the project doesn't ship a calibration
   cache yet, so it'll fall back to FP16 for unquantized layers — the
   flag is there for Phase 5 hardening.
5. **Hardware compatibility.** `HardwareCompatibilityLevel.AMPERE_PLUS`
   trades 5–10 % inference speed vs. arch-specific kernels for a single
   `.plan` that runs on every machine in the fleet. Override with
   `--hardware-compat none` for benchmark-grade builds.
6. **Optimization profile.** If any of `--min/--opt/--max-shape` is
   given, all three are required and `--input-name` must point to the
   tensor. The shape parser accepts `NxCxHxW`-style strings so the CLI
   stays terse: `--max-shape 1x3x48x320`.

After `build_serialized_network()` returns the bytes, the script
re-deserializes the engine just to dump its I/O signatures — same code
path as `inspect_engine.py`. This confirms the build at the CLI without
needing a follow-up command.

#### `inspect_engine.py` — why a separate script

Engines outlive the build process. Six months from now, somebody
upgrades PaddleOCR, re-exports the recognizer, and the OCR result silently
drops one character because the output tensor was renamed from
`softmax_2.tmp_0` to `softmax_2`. The C++ side will throw a
`TrtException("tensor not found")` at startup, which is the right
behavior — but `inspect_engine.py` lets the operator see that mismatch
in seconds without booting the server, just by pointing it at the new
`.plan`.

It also prints the per-profile `(min, opt, max)` shape ranges for
dynamic inputs, which is the only way to confirm a converted engine's
profile actually matches what the C++ recognizer was sized for.

#### Why the upstream export commands are documented but not scripted

Both YOLOv9 and PaddleOCR ship their own canonical exporters as part of
their training repos. Mirroring those commands into a local Python
script would (a) immediately drift the moment upstream bumps a flag,
and (b) drag every dependency of `yolov9` and `paddleocr` into this
project's environment for a step that runs once. The README lists the
exact upstream commands instead, with the parameter values that produce
ONNX matching the C++ side's expectations (e.g. `--topk-all 100` so
EfficientNMS_TRT emits the `[1, 100, 4]` `det_boxes` shape that the
detector's `max_detections_` was sized for).

This is the same pattern the project uses elsewhere — never hand-write
canonical text; reference the canonical source.

#### What's *not* in this milestone

- Engine artifacts (`.plan` files). They're per-host, depend on the
  exact TRT version + GPU arch, and are gitignored under
  `server/models/`. Operator builds them at deploy time from the
  upstream ONNX.
- INT8 calibration cache. The flag is wired but the calibration data
  pipeline is a Phase 5 concern.
- Multi-batch optimization profiles. The C++ side runs one camera
  frame at a time; multi-image batching is a Phase 4.3.x feature and
  this script will gain a `--max-batch` flag when that lands.

---

## Previous milestone — Phase 4.2.4: ALPR pipeline orchestrator

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/alpr_pipeline.hpp`](server/inference/include/inference/alpr_pipeline.hpp)
> and [`server/inference/src/alpr_pipeline.cpp`](server/inference/src/alpr_pipeline.cpp).

### What I built

`AlprPipeline` is the single class the rest of the server (gate
controller, fusion engine, gRPC service) talks to for license-plate
reads. It owns both inference backends from the previous milestones,
chains them, and exposes one method:

```cpp
std::vector<PlateReading> AlprPipeline::process(const cv::Mat& frame_bgr);
```

`PlateReading` carries the detector geometry **and** the OCR result
together, so a downstream consumer never has to correlate two
parallel arrays:

```cpp
struct PlateReading {
    cv::Rect2f  box;             // detector box in original-frame pixels
    std::string text;            // CTC-decoded plate string
    float       detection_score; // YOLO confidence
    float       ocr_score;       // mean per-char CTC confidence
};
```

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/alpr_pipeline.hpp` | Public API: `PlateReading`, `AlprPipeline::Config` (bundles detector + recognizer config + crop policy), move-only pipeline class. |
| `server/inference/src/alpr_pipeline.cpp` | Implementation: top-K cap, margin-expanded crop with frame-edge clamp, batched OCR, joined output assembly. |
| `server/inference/CMakeLists.txt` (updated) | Adds `alpr_pipeline.cpp` to the `gate_inference` static library. |

### Technical detail

#### Why one class instead of free functions

Detection and recognition share three traits that argue for a single
owning object: (1) both hold non-trivial scratch buffers that should be
allocated once at startup; (2) both wrap a `TrtEngine` whose CUDA stream
should not outlive the engine; (3) the *policy* knobs (margin, top-K,
OCR floor) need to live somewhere that isn't either backend. A pipeline
class concentrates ownership in one place — when the gate controller
constructs an `AlprPipeline`, two TensorRT engines and their CUDA
streams come up as a unit, and the destruction order at shutdown is
guaranteed correct because both backends are members.

The class is move-only and noexcept-movable for the same reason as the
backends: the eventual multi-camera setup will hold a
`std::vector<AlprPipeline>` (one per camera lane), and reallocation must
not run an engine destructor by accident.

#### Top-K crop policy

`detector_.detect()` returns boxes sorted by descending confidence.
The pipeline takes the top
`Config::max_plates_per_frame` (default 8) and discards the rest before
OCR is invoked. The cap exists for two reasons:

- **Latency protection on noisy frames.** EfficientNMS still emits up
  to `max_detections` boxes (typically 100). On a clean residential gate
  frame that's almost always 1 plate, but on a wide-angle parking-lot
  shot the recognizer would otherwise be invoked dozens of times for
  low-confidence noise.
- **Predictable upper bound on OCR latency.** Phase 4.3 (fusion engine)
  will run on a fixed frame budget; pinning the OCR fan-out makes that
  budget computable.

#### Crop with margin and clamp

CRNN models are trained on plates with a small border of background
context. Cropping flush to the YOLO box trims the leftmost/rightmost
character — a known failure mode that drops one or two characters from
the OCR output. `expand_and_clamp_()` pads each detector box by
`Config::crop_margin` (default 8 %) of its width/height on every side,
then clamps the resulting ROI to the frame so a plate detected at the
edge of view doesn't index outside the image.

The expansion is done in float (`cv::Rect2f` arithmetic), then quantized
once at the end — `floor` for the origin and `ceil` for the size — so a
fractional 0.5 px never costs a column. Boxes that clamp to fewer than
4×4 pixels are dropped before OCR is called; the recognizer would
reject the empty crop anyway and this keeps the failure local.

#### Batched OCR

The `recognize_batch` overload added in 4.2.3 isn't yet truly batched at
the TensorRT level — it loops the single-image path. The pipeline still
calls it (instead of looping itself) because that's the API surface
that **will** become batched in Phase 4.3 without changing the
pipeline. When the recognizer's enqueue path grows real batch support,
the pipeline gets the speedup for free and `process()` stays unchanged.

#### Output policy

OCR results are returned **unfiltered** by `ocr_score` — the
`Config::ocr_confidence_floor` field stores the threshold but
`process()` does not apply it. This is deliberate: low-confidence reads
are useful telemetry (a guard reviewing the dashboard wants to see the
"almost-recognized" plates) and the gate-control policy is the
authoritative consumer of the threshold. Encoding the policy at the
pipeline boundary would force the dashboard to either re-derive it or
read filtered data.

The result list is in detection-score order (inherited from
`YoloPlateDetector::detect`), so callers can take `result[0]` as "the
most likely primary plate in this frame" without resorting.

#### Allocation discipline

`process()` itself allocates exactly two `std::vector`s per frame
(`crops` and `kept`), each pre-reserved to `n_keep`. The `cv::Mat`
crops use OpenCV's reference-counted pixel data — `frame_bgr(roi)` is
an O(1) view; the explicit `.clone()` produces an independent buffer
the recognizer can safely consume. No CUDA allocations happen here;
both engines' device buffers were sized at `load()` time and are reused.

#### Verified compile

```
[1/7] Scanning .../alpr_pipeline.cpp for CXX dependencies
[2/7] Generating CXX dyndep file
[3/4] Building CXX object .../alpr_pipeline.cpp.o
[4/4] Linking CXX static library libgate_inference.a
```

Built against TensorRT 10.16.1, CUDA 13.1.115, OpenCV 4.14.0, GCC
14.2.0 with `-std=c++20 -Wall -Wextra -Wpedantic -Werror` — no
diagnostics. clang-format pass applied to match the project's
`.clang-format` (Google base, `ColumnLimit 100`, `IndentWidth 4`,
`IncludeBlocks Regroup`) so the CI Lint job stays green.

---

## Previous milestone — Phase 4.2.3: PaddleOCR plate-text recognizer

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/paddle_ocr_recognizer.hpp`](server/inference/include/inference/paddle_ocr_recognizer.hpp)
> and [`server/inference/src/paddle_ocr_recognizer.cpp`](server/inference/src/paddle_ocr_recognizer.cpp).
> Dictionary asset at
> [`server/models/dict_kenya_plates.txt`](server/models/dict_kenya_plates.txt).

### What I built

A PaddleOCR PP-OCRv4-compatible plate-text recognizer that takes a
cropped plate image (the typical output of `YoloPlateDetector::detect`)
and returns a `RecognizedPlate { text, confidence }`. Like the
detector, it sits directly on top of the `TrtEngine` wrapper from
4.2.1 and uses the same async-enqueue / single-sync execution model so
the upcoming ALPR pipeline can chain detection → recognition without
ever reading the host CPU between models.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/paddle_ocr_recognizer.hpp` | Public API: `RecognizedPlate` struct, `PaddleOcrRecognizer::Config`, move-only recognizer class. |
| `server/inference/src/paddle_ocr_recognizer.cpp` | Implementation: aspect-preserving resize + right-pad, per-channel mean/std normalization, async TRT inference, greedy CTC decode. |
| `server/models/dict_kenya_plates.txt` | 36-character dictionary (0–9, A–Z) covering every character that can appear on a Kenyan civilian or government plate. |
| `server/models/README.md` | Explains what model assets live in the tree (dictionaries) vs. what is built locally and gitignored (`.plan` engines, `.onnx` exports). |

### Technical detail

#### Preprocessing — what PaddleOCR actually expects

PP-OCRv4's plate recognizer is a CRNN-style network: a CNN backbone
that emits a sequence of feature columns, fed into a CTC head. The
input contract is unusual:

- **Fixed input height (48 px)** — required, because the CNN backbone's
  vertical stride collapses height to 1 in the feature map.
- **Variable input width up to a maximum (320 px)** — the recognizer
  reads left-to-right, so wider crops give more time steps but the
  trained max is 320.
- **Aspect-preserving resize** — squashing a wide plate into a square
  destroys character geometry; the model is trained on aspect-preserved
  inputs zero-padded on the right.

`preprocess_()` does exactly that: resize so height = 48 and width =
`round(48 × aspect)` clamped to `[1, 320]`, then `copyTo` into a
`(48 × 320, BGR, zero-padded)` canvas. The CTC head treats those
zero-padded columns as low-energy time steps and decodes them as blanks,
which the post-processor strips — so padding has no semantic effect on
the output text.

#### Normalization

PP-OCRv4 was trained with `(pixel/255 - mean) / std`, default
`mean = std = (0.5, 0.5, 0.5)`. With those symmetric values the
BGR-vs-RGB channel order is irrelevant, so the recognizer reads
OpenCV's native BGR directly and avoids a `cvtColor` round trip. The
arithmetic is per-channel (`cv::split` → subtract → divide), which keeps
us off `opencv_dnn::blobFromImage` and shaves a heavy module out of the
link line.

#### CTC greedy decode

The recognizer's output is `[1, T, C]` post-softmax probabilities. The
decoder is the standard CTC greedy:

```
for each time step t in [0, T):
    c = argmax_c output[t, c]
    if c == 0 (blank) or c == prev: skip            # CTC blank + repeat collapse
    text   += dictionary[c - 1]
    conf   += output[t, c]
    prev   = c
return (text, conf / kept_count)
```

Per-character confidence is the argmax probability at that time step;
overall plate confidence is the mean of those per-character values.
This is the right summary statistic for a downstream allow-list match —
a single low-confidence character in a 7-character plate drops the
score visibly, but a strong reading on the rest still indicates a high-
quality OCR.

A future enhancement (Phase 4.3 fusion engine) will use **per-character
confidence** rather than the mean to gate ambiguous chars (e.g.
`O` vs `0`) against the allow-list, but the mean is the right v1.

#### Dictionary contract

`dictionary_path` points to a UTF-8 text file with one character per
line. The model output's class 0 is the CTC blank token; class `i+1`
maps to dictionary line `i`. The recognizer enforces this on load —
if the model's class count doesn't equal `dictionary.size() + 1`, it
throws immediately with a descriptive `TrtException` so the failure
mode is "won't start" rather than "OCRs garbage".

The committed dictionary is tuned for **Kenya plates** specifically —
the format is `KXX 000X` (three letters + three digits + one letter),
and the 36-character vocabulary (0–9, A–Z) keeps the classifier head
small. Multi-region deployments swap the dictionary file without a
recompile; `server/models/README.md` documents the convention.

#### Allocation discipline

Identical to the detector: two host scratch buffers (`input_chw_`,
`output_logits_`), sized once at `load()` from the engine's resolved
shapes and reused for every recognition call. Per-frame inference
allocates only OpenCV's working memory for the resize + split.

#### Verified compile

```
[1/7] Scanning .../paddle_ocr_recognizer.cpp for CXX dependencies
[2/7] Generating CXX dyndep file
[3/5] Building CXX object .../paddle_ocr_recognizer.cpp.o
[4/5] Linking CXX static library libgate_inference.a
```

Built against the same toolchain as 4.2.1 and 4.2.2 (TensorRT 10.16.1,
CUDA 13.1.115, OpenCV 4.14.0, GCC 14.2.0,
`-std=c++20 -Wall -Wextra -Wpedantic -Werror`) — no diagnostics.

---

## Previous milestone — Phase 4.2.2: YOLOv9 plate detector

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/yolo_plate_detector.hpp`](server/inference/include/inference/yolo_plate_detector.hpp)
> and [`server/inference/src/yolo_plate_detector.cpp`](server/inference/src/yolo_plate_detector.cpp).

### What I built

A single-camera YOLOv9 license-plate detector built directly on the
`TrtEngine` wrapper from 4.2.1. It takes a BGR `cv::Mat` and returns a
`std::vector<PlateDetection>` whose boxes are already in the original
image's pixel coordinate frame — the call site does not need to know
anything about model input size, letterboxing, or NMS.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/yolo_plate_detector.hpp` | Public API: `PlateDetection` struct, `YoloPlateDetector::Config`, move-only detector class. |
| `server/inference/src/yolo_plate_detector.cpp` | Implementation: letterbox, BGR→RGB normalization, CHW pack, async inference, EfficientNMS_TRT decode, coordinate unmap. |
| `server/inference/CMakeLists.txt` (updated) | Adds OpenCV (core + imgproc) to `gate_inference`'s public link line. |

### Technical detail

#### Why model-side NMS

The detector targets ONNX exports produced by the official `yolov9` repo
with the `--end2end` flag, which embeds the standard
`EfficientNMS_TRT` plugin in the model graph. The engine therefore emits
already-NMSed detections via four output tensors — `num_dets`,
`det_boxes`, `det_scores`, `det_classes` — and the C++ side never has to
implement anchor decoding or non-max suppression. This pushes
~2 ms of CPU work onto the GPU where it overlaps with the rest of the
forward pass, and keeps the call-site code under 200 lines.

The detector still applies a `confidence_floor` filter on the way back
out as defense-in-depth: the EfficientNMS thresholds are baked in at
export time, but the deployment may want a stricter floor without
re-exporting the engine.

#### Preprocessing pipeline

`YoloPlateDetector::detect(const cv::Mat& bgr)` does the standard
YOLO-family preprocessing in three OpenCV steps:

1. **Letterbox.** `letterbox_()` resizes the source frame to fit inside
   the model's input canvas (default 640×640) preserving aspect ratio,
   then pads the remainder with neutral gray `(114, 114, 114)` — the
   YOLOv9 / Ultralytics convention. The `(scale, pad_x, pad_y)` triple
   is captured so detection boxes can be unmapped exactly.
2. **Color + dtype.** `cv::cvtColor(... BGR2RGB)` then `convertTo(...,
   CV_32FC3, 1/255)`. Two function calls; OpenCV does the SIMD work.
3. **HWC → CHW.** `cv::split` writes the three planes directly into a
   contiguous `std::vector<float>` host buffer that the wrapper
   pre-allocated at `load()` time, so per-frame inference does no heap
   allocation in the hot path.

The host buffer is then handed to `TrtEngine::enqueue` as a
`std::span<const std::byte>` keyed by the input tensor name (`"images"`
by default).

#### Output decoding

`enqueue` is followed by a single `sync()` (we don't yet pipeline
detection with downstream OCR — that's 4.2.4 territory). The four
output buffers are then walked once:

```
for i in [0, num_dets[0]):
    score = scores[i]
    if score < confidence_floor: continue
    (x1, y1, x2, y2) = boxes[i*4 : i*4+4]      // letterboxed-input space
    x1 = max(0, (x1 - pad_x) / scale)          // → original-image space
    y1 = max(0, (y1 - pad_y) / scale)
    x2 = min(W, (x2 - pad_x) / scale)
    y2 = min(H, (y2 - pad_y) / scale)
    if x2 <= x1 or y2 <= y1: continue          // degenerate after clamp
    emit PlateDetection{box, score, class_id}
```

Detections are returned sorted by descending confidence so the ALPR
pipeline (4.2.4) can apply a top-K crop policy without resorting.

#### Allocation discipline

Five host scratch buffers (`input_chw_`, `num_dets_host_`, `boxes_host_`,
`scores_host_`, `classes_host_`) are sized once at `load()` from the
context-resolved output shapes and reused for every frame. The detector
makes zero allocations in the per-frame path beyond OpenCV's internal
working memory for the `cvtColor`/`convertTo`/`split` steps.

The `max_detections` constant comes from the engine itself —
`engine_->context()->getTensorShape("det_boxes")` returns
`[1, max_det, 4]` after the input shape is pinned at load time. This
keeps the C++ side automatically in sync with however the ONNX export
was configured.

#### CMake: surviving OpenCV-with-CUDA on a modern toolchain

OpenCV was source-built against CUDA 13.1, which means
`OpenCVConfig.cmake` unconditionally calls
`find_host_package(CUDA REQUIRED)` — the legacy `FindCUDA` module.
CMake 3.27+ defaulted policy `CMP0146` to `NEW`, which removes that
module, and the policy doesn't propagate through vcpkg's
`find_package` wrapper. Rather than fight scoping, the build pre-fills
the half-dozen `CUDA_*` variables that OpenCV's config actually reads —
sourced from the modern `CUDA::cudart` / `CUDA::cublas` /
`CUDA::cufft` / `CUDA::nppc` / `CUDA::nppial` / `CUDA::npps` imported
targets that `find_package(CUDAToolkit)` provides — and stubs out
`find_cuda_helper_libs` as a no-op since we've already populated the
libraries it would have found. OpenCV's `if(NOT CUDA_FOUND)` short-
circuits cleanly and the rest of its config proceeds normally.

This is documented inline in `server/inference/CMakeLists.txt` so the
next person who reads it doesn't have to re-derive the chain.

#### Verified compile

```
[1/6] Scanning .../trt_engine.cpp for CXX dependencies
[2/6] Scanning .../yolo_plate_detector.cpp for CXX dependencies
[3/6] Generating CXX dyndep file ...
[4/6] Building CXX object .../trt_engine.cpp.o
[5/6] Building CXX object .../yolo_plate_detector.cpp.o
[6/6] Linking CXX static library libgate_inference.a
```

Built against TensorRT 10.16.1, CUDA 13.1.115, OpenCV 4.14.0 (CUDA
source-build), GCC 14.2.0 with `-std=c++20 -Wall -Wextra -Wpedantic
-Werror` — no diagnostics.

---

## Previous milestone — Phase 4.2.1: TrtEngine RAII wrapper

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/trt_engine.hpp`](server/inference/include/inference/trt_engine.hpp)
> and [`server/inference/src/trt_engine.cpp`](server/inference/src/trt_engine.cpp).

### What I built

A modern C++20 RAII wrapper around the TensorRT 10.x runtime API. It owns the
full inference state for one engine — the deserialized `ICudaEngine`, an
`IExecutionContext`, a CUDA stream, and per-binding device buffers — and
exposes a name-based, exception-throwing interface that the rest of the
inference layer (YOLOv9 detector, PaddleOCR recognizer, ALPR pipeline) is
built on top of.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/trt_engine.hpp` | Public API: `TrtException`, `TrtLogger`, `TensorIo` descriptor, move-only `TrtEngine` class. |
| `server/inference/src/trt_engine.cpp` | Implementation: engine deserialization, binding introspection, dynamic-shape buffer sizing, async H↔D + `enqueueV3` execution path. |
| `server/inference/CMakeLists.txt` | Builds `gate_inference` static lib; locates TensorRT headers/libs via standard system paths or `-DTENSORRT_ROOT=…`. |
| `server/CMakeLists.txt` | Adds the `inference/` subtree under `BUILD_SERVER=ON`. |
| Root `CMakeLists.txt` | Conditionally pulls in `server/` only when CUDA + spdlog are resolved, mirroring the same graceful-skip pattern used for `shared/proto/` and `tests/`. |

### Technical detail

#### Why a hand-written wrapper

TensorRT's runtime API (`IRuntime`, `ICudaEngine`, `IExecutionContext`)
returns raw pointers, uses noexcept return-code error handling, and in TRT
10.x deletes via the C++ `delete` operator (the older `destroy()` virtual is
gone). Calling that surface directly from inference code would scatter
`if (!ok) return false;` checks through every detector and recognizer. The
wrapper:

- Concentrates all error handling at one boundary — every TRT failure
  becomes a `gate::inference::TrtException` with a descriptive message
  (failing call name + offending tensor + offending shape where relevant).
- Takes ownership of the runtime / engine / context / CUDA stream / device
  buffers via `unique_ptr` with custom deleters, so the destructor frees
  resources in the only correct order: device buffers → stream → context →
  engine → runtime.
- Exposes name-based binding (`engine->getIOTensorName(i)` /
  `setTensorAddress(name, ptr)`) so re-exporting an engine with renumbered
  bindings doesn't break call sites — they reference tensors by string.
- Is move-only and noexcept-movable, which lets it live inside containers
  (`std::vector<TrtEngine>`) for the multi-engine pipeline that Phases 4.2.2
  and 4.2.3 will assemble.

#### Engine load path

`TrtEngine::load(path, logger)`:

1. Reads the serialized `.plan` file into a `std::vector<std::byte>` in one
   shot. The plan is opaque bytes; we do not parse it — TensorRT does.
2. `nvinfer1::createInferRuntime(logger)` produces the runtime. The supplied
   logger is a TensorRT `ILogger` reference; the project's default
   implementation, `TrtLogger`, bridges TRT severities to spdlog levels
   (`kINTERNAL_ERROR → critical`, `kERROR → err`, `kWARNING → warn`, etc.)
   with a configurable threshold (default `kWARNING` to keep INFO chatter
   out of the production log).
3. `runtime_->deserializeCudaEngine(blob.data(), blob.size())` reconstructs
   the engine. A null return raises `TrtException` with the engine path so
   plan corruption surfaces immediately at startup, never at the first
   inference.
4. `engine_->createExecutionContext()` produces the per-thread context.
5. A dedicated CUDA stream is created via `cudaStreamCreate`. Every H↔D
   copy and every kernel launch is enqueued on this stream, so `sync()` is
   the single observation point for all in-flight work.

#### Binding introspection

After the context is created, the loader walks
`engine_->getNbIOTensors()` and builds a `TensorIo` descriptor for each
binding:

```cpp
struct TensorIo {
    std::string             name;       // canonical TRT tensor name
    nvinfer1::Dims          shape;      // -1 marks dynamic dims
    nvinfer1::DataType      dtype;
    bool                    is_input;
    std::size_t             elem_size;  // bytes per element
};
```

Two parallel containers index by binding position (`tensors_`,
`device_buffers_`, `device_buffer_bytes_`); a `name_to_index_` hash map
gives O(1) name lookup. All public methods take `std::string_view` and
resolve through that map so call sites don't carry binding numbers.

#### Dynamic-shape device buffer allocation

The hardest part of a generic TRT wrapper is sizing buffers when the engine
has dynamic dimensions (the `-1` dims YOLOv9 uses for `batch` and the OCR
recognizer uses for sequence length). The wrapper handles all three cases
in `allocate_buffers_()`:

1. **Static binding** — `volume(shape) * elem_size` is allocated directly.
2. **Dynamic input** — the wrapper queries
   `engine_->getProfileShape(name, 0, OptProfileSelector::kMAX)` and
   allocates for that maximum. Subsequent `set_input_shape()` calls with
   any in-profile shape reuse the same buffer.
3. **Dynamic output** — output shapes can also be `-1` (e.g. NMS-derived
   detection counts). The wrapper does a second pass: it primes every
   dynamic input with its kMAX shape via `setInputShape`, then asks the
   context to resolve each output via `getTensorShape(name)`, and allocates
   from there.

`buffer_bytes(name)` consults the context's *current* view of the shape, so
after `set_input_shape()` the reported size shrinks to match the runtime
shape — the device buffer is over-allocated (safe) but `enqueue()` only
copies the bytes the model actually consumes/produces.

#### Async execution path

`enqueue(host_in, host_out)` performs the entire ALPR-step lifecycle on the
internal stream:

1. **Re-bind every tensor address.** TRT 10's `enqueueV3` requires an
   address for every input and output to have been set since the last
   `setInputShape` call. The wrapper rebinds unconditionally so callers
   don't accidentally inherit a stale binding from a prior context use.
2. **Async H→D for every input** in the supplied map.
   `cudaMemcpyAsync(..., cudaMemcpyHostToDevice, stream_)`. Each input span
   is size-checked against `buffer_bytes(name)`; mismatches throw before
   any DMA is issued.
3. **Async forward pass.** `context_->enqueueV3(stream_)`. A `false` return
   raises `TrtException` with a hint about the most likely cause (unbound
   input or unset shape).
4. **Async D→H for every requested output**, with the same size-check
   discipline.
5. **`sync()`** is a separate, optional call. The split lets the caller
   overlap CPU work (post-processing, fusion, gRPC reply assembly) with
   GPU work, and gives the ALPR pipeline a place to insert a CUDA event
   for cross-stream barriers later.

Every CUDA call goes through a `check_cuda(status, "what")` helper that
converts the error code into `cudaGetErrorString(...)` text so failures in
production logs are immediately diagnosable.

#### Build integration

`server/inference/CMakeLists.txt` locates TensorRT through `find_path` /
`find_library` rather than `find_package` because TensorRT does not ship a
CMake config. The result is wrapped in an `IMPORTED` target,
`TensorRT::nvinfer`, with the header path attached as `SYSTEM` includes so
the project-wide `-Wall -Wextra -Wpedantic -Werror` doesn't flag TRT's
own headers. The library links `CUDA::cudart` from `CUDAToolkit` and
`spdlog::spdlog` from vcpkg.

The root `CMakeLists.txt` only descends into `server/` when `BUILD_SERVER`
is on **and** `find_package(spdlog CONFIG QUIET)` succeeds. CI's
non-toolchain Build job stays green because the inference module is simply
skipped with a clear status message; local development with the vcpkg
toolchain pulls the full dependency closure (gRPC, protobuf, drogon,
spdlog, sqlite3, fmt, nlohmann-json, Catch2, cli11) and builds normally.

#### Verified compile

Built clean against:

- TensorRT 10.16.1 (`libnvinfer.so` at `/usr/lib/x86_64-linux-gnu/`)
- CUDA Toolkit 13.1.115 (nvcc + cudart)
- GCC 14.2.0 with `-std=c++20 -Wall -Wextra -Wpedantic -Werror`
- spdlog 1.17.0 from vcpkg

Build output:

```
[1/4] Scanning .../trt_engine.cpp for CXX dependencies
[2/4] Generating CXX dyndep file ...
[3/4] Building CXX object .../trt_engine.cpp.o
[4/4] Linking CXX static library libgate_inference.a
```

No warnings, no diagnostics — strict warnings are kept on for first-party
code.

---

## Previous milestone — Phase 4.1: gRPC wire contract

> **Pulled this off on 2026-04-25.** Full code in
> [`shared/proto/`](shared/proto/) and [`tests/proto/`](tests/proto/).

### What I built

A single canonical `gate.v1` protobuf schema —
[`shared/proto/gate_service.proto`](shared/proto/gate_service.proto) — that
defines every byte that crosses a process boundary in the system. Firmware,
inference server, dashboard backend, and simulation all generate stubs from
this one file.

| Artifact | Purpose |
|---|---|
| `shared/proto/gate_service.proto` | 25 messages, 6 enums, 3 services, 10 RPC methods. |
| `shared/proto/CMakeLists.txt` | Generates `gate_proto` static lib (messages + gRPC stubs) for host C++ targets. Consumed by server, dashboard, and sim. |
| `shared/proto/README.md` | Developer reference for the wire contract. |
| `tests/proto/proto_contract_test.cpp` | Catch2 round-trip tests for telemetry, decisions, OTA, and the bidirectional `ControlEnvelope`. |
| `tests/proto/validate_descriptor.py` | protoc-only descriptor validator that runs in CI without the full C++ toolchain. |
| `.github/workflows/proto.yml` | CI job that compiles the descriptor and runs the validator on every change. |

### Technical detail

#### Service surface

Three services with deliberately chosen streaming patterns:

| Service / Method | Streaming kind | Why |
|---|---|---|
| `FieldControllerService.Control` | bidi (client+server stream) | One TCP connection per gate, kept alive for the lifetime of the field controller. Telemetry flows up at 1 Hz; commands flow down on demand; acks/faults multiplex over the same stream — half the connection count, no race between an incoming command and an outgoing ack. |
| `FieldControllerService.DeliverOta` | server stream | OTA images are 1.5–2 MB. Streaming 4 KB chunks (one ESP32 flash sector each) lets the firmware hash incrementally and abort early on signature mismatch without buffering the whole image in PSRAM. |
| `FieldControllerService.ReportOtaProgress` | client stream | Firmware emits a progress event for each phase (`DOWNLOADING → VERIFYING → INSTALLING → REBOOTING → COMPLETE`). One stream per OTA session. |
| `FieldControllerService.SubmitDetection` | unary | A single detection frame from the GPU pipeline returns one `AuthDecision`. Used inside the server and from the simulation harness. |
| `DashboardService.Subscribe` | server stream | The web UI gets a filtered, replayable feed of every `DashboardEvent` (decisions, telemetry, faults, OTA progress, commands, acks) via the `since_event_id` resume cursor. |
| `DashboardService.IssueCommand` / `Authorize` | unary | Manual override and synchronous test calls. |
| `AdminService.{Upsert,List,Delete}Allowlist` | unary | CRUD over the per-site allowlist. |

#### Key design choices

1. **`oneof ControlEnvelope.payload`** multiplexes telemetry, command,
   command-ack and fault events over the bidirectional stream. The contract
   test asserts the four payload tags and their stable field numbers
   (`kTelemetry`, `kAck`, `kFault`, `kCommand`).

2. **`google.protobuf.Timestamp` + `Duration` everywhere** — no integer
   "milliseconds since epoch" or hand-rolled time fields. This keeps every
   client (C++, Python sim, JS dashboard) using the same time semantics.

3. **`DashboardEvent.event_id` is monotonic** per server boot; the
   `DashboardSubscription.since_event_id` field lets the web UI resume after
   a refresh without losing events.

4. **`OtaChunk` size is fixed at 4096 bytes** to match an ESP32 flash sector
   (the contract test verifies a full sector round-trips). This eliminates
   the partial-sector edge case in firmware OTA.

5. **`Telemetry` is bandwidth-budgeted at 256 bytes**. The contract test
   `Telemetry stays under 256-byte firmware budget` enforces this at build
   time so we don't accidentally bloat the over-the-air protocol when
   adding fields.

6. **Stable field numbers and enum values** are documented in
   `validate_descriptor.py`. Any change that moves a number is a breaking
   change, requires a `gate.v2` package, and must be approved via ADR.

#### Build integration

`shared/proto/CMakeLists.txt` invokes `protoc` with the gRPC C++ plugin to
emit `gate_service.pb.{h,cc}` and `gate_service.grpc.pb.{h,cc}`, links them
into a single `gate_proto` static target, and re-exports the FileDescriptorSet
so other tools (gRPC reflection, dashboard codegen) can consume it. The
target is added by the root `CMakeLists.txt` whenever
`-DBUILD_PROTO=ON` (the default).

Generated code is compiled with `-Wno-unused-parameter -Wno-deprecated-declarations`
because `protoc` output is not under our control; first-party code remains
under the strict project-wide `-Wall -Wextra -Wpedantic -Werror`.

#### Validation strategy

Two layers, deliberately split:

- **`validate_descriptor.py`** runs in CI on every push that touches
  `shared/proto/`. It only needs `protoc` and Python's `google.protobuf` —
  no vcpkg toolchain bootstrap. It walks the descriptor and asserts:
    - All 25 expected messages exist.
    - All stable enum values have their documented numbers.
    - All 10 RPC methods exist with the correct client/server streaming kinds.
    - `ControlEnvelope` is a `oneof` named `payload` with the four
      documented variants.
- **`proto_contract_test.cpp`** is a Catch2 suite that round-trips every
  important message through `SerializeToString` / `ParseFromString`,
  verifies the `ControlEnvelope` oneof discriminator, and enforces the
  256-byte telemetry budget. It builds as part of the standard `tests/`
  target once the full vcpkg dependencies are in place.

#### Current validator output

```
Validating shared/proto/gate_service.proto
OK: package=gate.v1, syntax=proto3
OK: all 25 expected messages present
OK: enum VehicleClass has 9 stable values
OK: enum GateState has 7 stable values
OK: enum AuthVerdict has 5 stable values
OK: service FieldControllerService has 4 methods with correct streaming kinds
OK: service DashboardService has 3 methods with correct streaming kinds
OK: service AdminService has 3 methods with correct streaming kinds
OK: ControlEnvelope oneof has 4 expected payloads

All proto contract checks passed.
```

---

## Repository layout

```
gate-automation/
├── docs/                  # ADRs, diagrams, hardware build guides, env audit
│   ├── decisions/         # 11 ADRs (ADR-000 through ADR-010)
│   ├── diagrams/          # 7 Mermaid flowcharts
│   └── hardware/          # 9 component guides + master build book + KiCad PDF
├── hardware/
│   └── bom/               # Bill of materials with KES/USD pricing
├── shared/
│   ├── proto/             # ✅ Phase 4.1 — gRPC wire contract
│   └── include/           # Shared C++ headers
├── server/                # ⏳ Phase 4.2 — ALPR + LiDAR inference + fusion
├── firmware/              # ⏳ Phase 4.4 — ESP-IDF field controller firmware
├── simulation/            # ⏳ Phase 4.6 — virtual gate harness
├── dashboard/             # ⏳ Phase 4.7 — Drogon backend + SvelteKit frontend
├── deployment/            # ⏳ Phase 4.8 — systemd units + install scripts
├── tests/                 # Catch2 unit + contract tests
│   └── proto/             # ✅ Phase 4.1 — proto contract tests
├── scripts/bootstrap/     # Reproducible WSL2 dev environment scripts
└── .github/workflows/     # CI: build, test, lint, codeql, commitlint, proto
```

---

## Hardware

Single-gate prototype BOM:

- **Gate-side electronics:** KES 58,300 (~USD 448)
- **GPU server (RTX 4060 + i5):** KES 85,200 (~USD 655)
- **System total without motor:** **KES 143,500 (~USD 1,103)**
- **With sliding gate motor (CENTURION D5):** KES 208,500 (~USD 1,603)

Full BOM with sources and prices: [`hardware/bom/prototype-bom.md`](hardware/bom/prototype-bom.md).

Build book (9 step-by-step component guides + master assembly): [`docs/hardware/BUILD_BOOK.md`](docs/hardware/BUILD_BOOK.md).

Production PCB: 4-layer 100×80 mm field controller designed in KiCad 9.0
— [48-page design guide PDF](docs/hardware/10-pcb-design-kicad9.pdf).

---

## Tech stack

| Layer | Tools |
|---|---|
| Compute | NVIDIA GPU (CUDA 13.1, cuDNN 9.19, TensorRT 10.15) |
| Inference | YOLOv9 (plate detection), PaddleOCR (character recognition) |
| Vision | OpenCV 4.14 (CUDA-enabled, source build) |
| Server (C++) | C++20, CMake 3.31, Ninja, mold, ccache, vcpkg manifest mode |
| RPC | gRPC + Protobuf (single canonical schema in `shared/proto/`) |
| Dashboard backend | Drogon (C++ web framework) |
| Dashboard frontend | SvelteKit |
| Field controller | ESP32-S3-WROOM-1-N16R8 + W5500 wired Ethernet |
| Firmware | ESP-IDF, modern C++ |
| OTA | Self-hosted, ed25519-signed, atomic swap with rollback |
| Tests | Catch2 v3, ASan/UBSan, clang-tidy, cppcheck, CodeQL |
| CI | GitHub Actions (matrix: GCC 13/14 × Clang 17/18 × Debug/Release) |

---

## Running it locally

### Bootstrap the dev environment (one-time, WSL2 Debian)

```bash
cd scripts/bootstrap
./01-toolchain.sh        # GCC, Clang, CMake, Ninja, mold, ccache
./02-cuda-stack.sh       # CUDA 13.1 + cuDNN 9.19 + TensorRT 10.15
./03-opencv-cuda.sh      # OpenCV 4.14 from source with CUDA
./04-vcpkg.sh            # vcpkg + manifest install
./05-esp-idf.sh          # ESP-IDF for firmware development
```

Verify with the GPU smoke test (see [`docs/env-audit.md`](docs/env-audit.md)).

### Validate the proto schema

```bash
python3 tests/proto/validate_descriptor.py
```

### Configure & build (Phase 4.1)

```bash
cmake --preset release
cmake --build --preset release --target gate_proto
ctest --preset release
```

---

## Documentation

- [Environment audit](docs/env-audit.md) — every tool, every version, every challenge solved during bootstrap
- [Architecture decisions (ADRs)](docs/decisions/) — 11 records covering license, RPC, web framework, MCU, LiDAR, camera, gate actuator, OTA, model licensing
- [System diagrams](docs/diagrams/) — 7 Mermaid flowcharts (system, ALPR, LiDAR, fusion, gate state machine, OTA, sim mode)
- [Hardware build book](docs/hardware/BUILD_BOOK.md) — bench prototype to working gate in ~3.5 h
- [PCB design guide (PDF)](docs/hardware/10-pcb-design-kicad9.pdf) — KiCad 9.0 schematic-to-Gerbers
- [Bill of materials](hardware/bom/prototype-bom.md) — every part with KES + USD pricing and Nairobi/AliExpress sources

---

## License

[GPL-3.0](LICENSE) — chosen for compatibility with YOLOv9's GPL-3.0 license
(see [ADR-000](docs/decisions/ADR-000-license.md) and
[ADR-010](docs/decisions/ADR-010-model-licensing.md)).

## Contributing

This is a personal portfolio project. External contributions are not yet
accepted, but feedback via GitHub issues is welcome.
