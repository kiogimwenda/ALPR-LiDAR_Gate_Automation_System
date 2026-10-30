# scripts/export-models: from ALPR models to TensorRT engines

The C++ inference layer (`server/inference/`) consumes serialized TensorRT
engines (`.plan` files). This directory has the tooling that:

- fetches the pinned upstream models;
- adapts them to the C++ engine contract;
- builds the engines on the deployment host;
- evaluates the models on the Kenya test set.

Model choice, licences and provenance are recorded in
[`docs/decisions/ADR-014-alpr-model-selection.md`](../../docs/decisions/ADR-014-alpr-model-selection.md).

## Files

| File | Purpose |
|---|---|
| `prepare_onnx.sh` | **Model entry point.** Fetches and SHA-256-verifies the two deployed upstream models, grafts EfficientNMS_TRT onto the detector, and stages `yolov9_plate.onnx`, `plate_ocr.onnx` and `plate_ocr_config.yaml` in `models/weights/` (git-ignored). |
| `fetch_models.py` | Downloads every Phase-1 candidate (or `--only prod`) into `models/downloads/`, pinned by URL and SHA-256. |
| `graft_efficientnms.py` | Replaces the ONNX NonMaxSuppression tail of the open-image-models end2end export with `EfficientNMS_TRT` (four outputs). Also writes `<onnx>.fp32_layers`: the box-decode and NMS layers that `build_engines.sh` keeps in FP32. |
| `convert_paddle_rec.sh` | Converts PaddleOCR rec checkpoints to ONNX with paddle2onnx 2.x (Python 3.12 venv). Used for evaluation only. |
| `build_engines.sh` | **Deployment entry point.** Builds both engines on this host's GPU (any RTX 20-series or newer). It writes `.meta` sidecars, honours `.fp32_layers`, copies the plate config, and skips engines already built for this GPU and TensorRT version. Uses the Python converter when TensorRT's Python bindings exist, else `trtexec`. Run by `install-server.sh --models`. |
| `convert_onnx_to_trt.py` | ONNX → `.plan` builder. FP16 by default; also supports INT8, dynamic-shape profiles, `--fp32-layers-file`, and native or `--hardware-compat` builds. Writes the `.meta` sidecar. |
| `inspect_engine.py` | Prints a `.plan`'s I/O tensors. Needs the TensorRT Python bindings; without them, `trtexec --loadEngine=<plan>` prints the same bindings. |
| `eval/` | Phase-1/2 evaluation (see below). |

## Step 1: build the ONNX models

```bash
scripts/export-models/prepare_onnx.sh          # → models/weights/
```

| Output | Model | I/O |
|---|---|---|
| `yolov9_plate.onnx` | open-image-models YOLOv9-s-608 + EfficientNMS_TRT (score 0.10, IoU 0.45, top-100) | `images` float 1×3×608×608 (114-gray letterbox, RGB, /255) → `num_dets` int32 [1,1], `det_boxes` [1,100,4] x1y1x2y2 in canvas pixels, `det_scores` [1,100], `det_classes` int32 [1,100] |
| `plate_ocr.onnx` | fast-plate-ocr `cct-s-v2-global` | `input` uint8 [N,64,128,3] RGB (plain stretch) → `plate` [N,10,37] slot probabilities over `0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_`, plus `region` [N,66] (ignored) |
| `plate_ocr_config.yaml` | The recognizer's alphabet, pad character, slot count and input size | read by `PlateRecognizer` |

The graft is deterministic. Its output SHA-256 is pinned in `prepare_onnx.sh`
(`55ac0d58…a5ac71`).

## Step 2: build the TensorRT engines (on the host that runs them)

```bash
scripts/export-models/build_engines.sh --onnx-dir models/weights --out-dir /opt/gate/models
```

The two engines are built as follows:

- **Detector:** fixed shape, FP16. The layers listed in
  `yolov9_plate.onnx.fp32_layers` stay in FP32, because in FP16 the box
  coordinates have 0.5-pixel resolution, which cost up to 16 IoU points on small
  plates. Latency is unchanged at 3.7 ms on the RTX 5060 Laptop. EfficientNMS_TRT
  comes from `libnvinfer_plugin`, which the C++ loader registers at startup.
- **Recognizer:** FP16, batch pinned to 1 (`input` 1×64×128×3), 1.25 ms.

## Step 3: verify

```bash
trtexec --loadEngine=/opt/gate/models/yolov9_plate.plan --iterations=1 | grep binding
trtexec --loadEngine=/opt/gate/models/plate_ocr.plan    --iterations=1 | grep binding
```

Expected:

```
Input binding for images with dimensions 1x3x608x608 and type fp32
Output binding for num_dets with dimensions 1x1 and type int32
Output binding for det_boxes with dimensions 1x100x4 and type fp32
Output binding for det_scores with dimensions 1x100 and type fp32
Output binding for det_classes with dimensions 1x100 and type int32

Input binding for input with dimensions 1x64x128x3 and type uint8
Output binding for plate with dimensions 1x10x37 and type fp32
Output binding for region with dimensions 1x66 and type fp32
```

gate-vision runs the deployed pair with:

```
--detector-engine yolov9_plate.plan --recognizer-mode slots \
--recognizer-engine plate_ocr.plan --plate-config plate_ocr_config.yaml
```

## The PaddleOCR CTC path (`--recognizer-mode ctc`)

`PlateRecognizer::Mode::kCtc` still accepts a PP-OCR-style CRNN with this
contract:

- input `x` 1×3×48×320, aspect-preserving resize, padding of 0 *after*
  normalisation, BGR;
- output [1, T, C] post-softmax, blank at index 0;
- C must equal the dictionary size plus 1 (37 for `dict_kenya_plates.txt`).

Stock PP-OCRv4/v5 rec models have 97, 438 or 838 classes, are rejected at load,
and scored at most 0.46 on the test set (0% on two-line plates). They would need
fine-tuning with the 36-character dictionary. After `convert_paddle_rec.sh`,
their output tensor is named `fetch_name_0`.

## Evaluation (`eval/`)

| File | Purpose |
|---|---|
| `testset/manifest.csv` | **Committed** Kenya test set: 166 Wikimedia Commons images and 225 hand-read plates, with source page, author, licence and SHA-256 per image. |
| `testset/gt_notes.txt`, `sweep_notes.txt`, `manual_boxes.txt` | The hand annotations the manifest is built from (`build_testset_manifest.py`). |
| `fetch_testset.py` | Re-downloads the images into `models/downloads/kenya-testset/images/` and verifies each SHA-256. |
| `collect_commons_candidates.py`, `propose_plates.py`, `build_testset_manifest.py` | How the test set was built: harvest, detector-seeded review sheets, manifest. |
| `alpr_candidates.py`, `eval_candidates.py`, `render_eval.py` | ONNX Runtime runners for every candidate, metrics, and annotated images. |
| `cpp_parity.py` | Scores `alpr_bench` (C++ / TensorRT) output: detector parity against ORT on the original ONNX, end-to-end accuracy against Python, and latency. |

```bash
python3 -I scripts/export-models/eval/fetch_testset.py
python3 -I scripts/export-models/eval/eval_candidates.py          # → models/downloads/eval/
build/debug/server/inference/alpr_bench --detector-engine … --recognizer-engine … \
    --plate-config … --images list.txt --out cpp_e2e.jsonl
python3 -I scripts/export-models/eval/cpp_parity.py --parity … --e2e cpp_e2e.jsonl
```

All weights, engines, images and results stay in the git-ignored
`models/downloads/` and `models/weights/`.

## Hardware-compatibility note

The system supports every NVIDIA RTX generation from the 20 series: Turing
(sm_75), Ampere (sm_86), Ada (sm_89) and Blackwell (sm_120).

- **Why engines are built on the host:** TensorRT's portable modes cannot cover
  that range. `AMPERE_PLUS` excludes RTX 20, and `SAME_COMPUTE_CAPABILITY`
  covers one generation. Engines are therefore built native, on the host that
  runs them.
- **The `.meta` sidecar:** each `.plan` gets one (build GPU, compute capability,
  TensorRT version, compatibility mode). The C++ loader checks it before
  deserializing and refuses a mismatched engine.
