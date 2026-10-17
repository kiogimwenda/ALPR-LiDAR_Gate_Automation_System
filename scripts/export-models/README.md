# scripts/export-models — ONNX → TensorRT engine tooling

The C++ inference layer (`server/inference/`) consumes serialized TensorRT
engines (`.plan` files). This directory has the Python tooling that turns an
ONNX model into one of those .plan files and verifies its I/O surface.

We **do not** mirror the upstream YOLOv9 / PaddleOCR export scripts here — both
projects publish their own canonical exporters and re-implementing them locally
would drift the moment they bump a version. Instead, this README documents
which upstream commands to run, and the scripts pick up from the resulting
`.onnx` file.

## Files

| File | Purpose |
|---|---|
| `build_engines.sh` | **Deployment entry point.** Builds both ALPR engines on the host's own GPU (any RTX 20-series or newer), writes `.meta` sidecars, skips engines already built for this GPU + TensorRT. Uses the Python converter when TensorRT's Python bindings exist, else `trtexec`. Run by `install-server.sh --models`. |
| `convert_onnx_to_trt.py` | ONNX → `.plan` builder. FP16 default, optional INT8, dynamic-shape profiles, native-to-this-GPU by default (`--hardware-compat same_cc` / `ampere_plus` optional), workspace sized to the GPU's VRAM, writes the `.meta` sidecar. |
| `inspect_engine.py` | Print a `.plan`'s I/O tensor names, shapes, and dtypes. Use this to verify upstream didn't rename a tensor before the C++ side fails on `setTensorAddress`. |

## Prerequisites

- An NVIDIA RTX 20-series or newer GPU (Turing sm_75 and up) with driver ≥ 580.
- TensorRT 10.x — the same version the C++ side links. `build_engines.sh`
  needs only the `trtexec` CLI (`libnvinfer-bin`); `convert_onnx_to_trt.py`
  needs the Python bindings of the *same* version.
- Engines are built **on the machine that runs them** (see the
  hardware-compatibility note below).

## Step 1 — get the ONNX models from upstream

### YOLOv9 plate detector

The C++ detector (`server/inference/src/yolo_plate_detector.cpp`) targets the
**`--end2end` ONNX export** of [WongKinYiu/yolov9][yolov9]. That path embeds
the `EfficientNMS_TRT` plugin in the model graph, so the engine emits already-
NMSed detections via four output tensors (`num_dets`, `det_boxes`,
`det_scores`, `det_classes`).

Upstream export command (run inside the yolov9 repo with the Kenya-plate
fine-tuned weights at `runs/train/.../weights/best.pt`):

```bash
python export.py \
    --weights runs/train/.../weights/best.pt \
    --include onnx_end2end \
    --img 640 \
    --simplify \
    --topk-all 100 \
    --iou-thres 0.45 \
    --conf-thres 0.25 \
    --device 0
```

This writes `runs/train/.../weights/best-end2end.onnx`.

### PaddleOCR PP-OCRv4 recognizer

The C++ recognizer (`server/inference/src/paddle_ocr_recognizer.cpp`) targets
the **PP-OCRv4 recognition CRNN** ONNX export. PaddleOCR ships a PaddlePaddle
checkpoint; convert it to ONNX with [paddle2onnx][p2o]:

```bash
paddle2onnx \
    --model_dir   ./inference/ch_PP-OCRv4_rec_infer \
    --model_filename       inference.pdmodel \
    --params_filename      inference.pdiparams \
    --save_file            ppocrv4_rec.onnx \
    --opset_version        16 \
    --enable_dev_version   True
```

Output: `ppocrv4_rec.onnx`. The default tensor names are `x` (input) and
`softmax_2.tmp_0` (output) — the C++ Config defaults match.

[yolov9]: https://github.com/WongKinYiu/yolov9
[p2o]: https://github.com/PaddlePaddle/Paddle2ONNX

## Step 2 — build the TensorRT engines

### YOLOv9 (fixed shape — EfficientNMS_TRT requires it)

```bash
python convert_onnx_to_trt.py \
    --onnx best-end2end.onnx \
    --plan ../../server/models/yolov9_plate.plan \
    --fp16
```

No `--min-shape/--opt-shape/--max-shape` because the export pinned
`1×3×640×640`. The script will print the four output tensor names at the end —
verify they match the defaults in `YoloPlateDetector::Config`.

### PaddleOCR (dynamic width)

```bash
python convert_onnx_to_trt.py \
    --onnx ppocrv4_rec.onnx \
    --plan ../../server/models/ppocrv4_rec.plan \
    --fp16 \
    --input-name x \
    --min-shape  1x3x48x32 \
    --opt-shape  1x3x48x160 \
    --max-shape  1x3x48x320
```

The C++ side preprocesses every plate to width 320, so `--max-shape` of
`1×3×48×320` matches the recognizer's worst case. `--opt-shape` is the
common-case width — picking 160 gives the best kernel selection for the
typical Kenya plate aspect (≈ 1:3.3).

## Step 3 — verify

```bash
python inspect_engine.py ../../server/models/yolov9_plate.plan
python inspect_engine.py ../../server/models/ppocrv4_rec.plan
```

Expected for YOLOv9:

```
[INPUT ] images       shape=(1, 3, 640, 640)  dtype=FLOAT
[OUTPUT] num_dets     shape=(1, 1)            dtype=INT32
[OUTPUT] det_boxes    shape=(1, 100, 4)       dtype=FLOAT
[OUTPUT] det_scores   shape=(1, 100)          dtype=FLOAT
[OUTPUT] det_classes  shape=(1, 100)          dtype=INT32
```

Expected for PP-OCRv4 (the `-1` on dim 3 is the dynamic width):

```
[INPUT ] x                  shape=(1, 3, 48, -1)
[OUTPUT] softmax_2.tmp_0    shape=(1, -1, 6625)   # T variable, C = dict + 1
```

If a tensor name is different, fix it in the corresponding C++ Config field —
do not rename it in the engine.

## Hardware-compatibility note

The system supports every NVIDIA RTX generation from the 20 series: Turing
(sm_75), Ampere (sm_86), Ada (sm_89) and Blackwell (sm_120). TensorRT's
portable modes cannot cover that range — `AMPERE_PLUS` requires sm_80 or newer
(it excludes every RTX 20 card) and `SAME_COMPUTE_CAPABILITY` covers one
generation — so engines are built **native, on the host that runs them**
(`--hardware-compat none`, the default). `build_engines.sh` does that at
install time and again whenever the GPU or TensorRT version changes.

Each `.plan` gets a `<plan>.meta` sidecar (build GPU, compute capability,
TensorRT version, compatibility mode). The C++ loader checks it before
deserializing and refuses a mismatched engine with a "rebuild on this host"
message, and rejects pre-Turing GPUs outright. `--hardware-compat ampere_plus`
is still available for a fleet that is entirely sm_80 or newer.

## Where the .plan files live

Built engines are written to `server/models/*.plan` and **are not committed to
git** — they're per-host artifacts that depend on the exact TRT version + GPU
arch. `server/models/README.md` documents the exclusion. CI does not need
them; the C++ unit tests (Phase 4.2.6) use a tiny fixture engine built on the
fly inside the test harness.
