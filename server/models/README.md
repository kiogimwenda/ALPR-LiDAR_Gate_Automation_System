# server/models/

Runtime assets the inference server loads at startup.

## What lives here

| File | Used by | Purpose |
|---|---|---|
| `dict_kenya_plates.txt` | `PlateRecognizer` (`--recognizer-mode ctc` only) | CTC dictionary for a PaddleOCR-style recognizer. One UTF-8 character per line. Index 0 in the model output is the CTC blank token; line `i` here corresponds to model class `i + 1`, so the model must have exactly 37 output classes. The shipped fixed-slot recognizer does not use it — its alphabet (`0-9A-Z` + pad `_`, the same 36 characters) comes from `plate_ocr_config.yaml`. |

## What does NOT live here

The TensorRT engine plans (`*.plan`, with their `.meta` sidecars) and ONNX
exports (`*.onnx`) are **not** committed. Engines are specific to the GPU
generation and TensorRT version they are built with, so they are built on
the machine that runs them — any NVIDIA RTX 20-series or newer GPU:

```bash
scripts/export-models/prepare_onnx.sh            # fetch + verify + graft → models/weights/
scripts/export-models/build_engines.sh --onnx-dir models/weights --out-dir server/models
```

This produces `yolov9_plate.plan` (YOLOv9-s-608 + EfficientNMS_TRT),
`plate_ocr.plan` (fast-plate-ocr cct-s-v2-global) and `plate_ocr_config.yaml`;
model choice and licences are in `docs/decisions/ADR-014-alpr-model-selection.md`.

On a deployed server `install-server.sh --models <onnx dir>` runs the same
script into `/opt/gate/models`. `gate-vision` takes the paths on its command
line (`--detector-engine`, `--recognizer-engine`; see
`deployment/config/gate-vision.env`).

## Why a Kenya-plate dictionary

Kenya civilian plate format is `KXX 000X` — three uppercase Latin
letters, three digits, one uppercase Latin letter check character.
Government plates and a handful of older formats use the same
character set. The 36-character dictionary committed here covers every
character that can legally appear on a KE plate, which keeps the
model's classifier head small (37 classes including the CTC blank) and
the per-class confidence high.

If the deployment expands beyond Kenya (PRC plates with Chinese
characters, EU plates with country prefixes, etc.) the dictionary
file is the single point of change — no recompile required.
