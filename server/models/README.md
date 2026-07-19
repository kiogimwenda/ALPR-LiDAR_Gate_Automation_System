# server/models/

Runtime assets the inference server loads at startup.

## What lives here

| File | Used by | Purpose |
|---|---|---|
| `dict_kenya_plates.txt` | `PaddleOcrRecognizer` | OCR character dictionary. One UTF-8 character per line. Index 0 in the model output is the CTC blank token; line `i` here corresponds to model class `i + 1`. |

## What does NOT live here

The TensorRT engine plans (`*.plan`) and ONNX exports (`*.onnx`) are
**not** committed. They are large binary artifacts produced from
upstream model weights via the conversion tooling that ships in
Phase 4.2.5 (`scripts/convert_onnx_to_trt.py`).

Build them locally with:

```bash
# Coming in Phase 4.2.5:
python3 scripts/convert_onnx_to_trt.py \
    --onnx  models/yolov9_plate.onnx \
    --plan  server/models/yolov9_plate.plan \
    --fp16

python3 scripts/convert_onnx_to_trt.py \
    --onnx  models/ppocr_plate_rec.onnx \
    --plan  server/models/ppocr_plate_rec.plan \
    --fp16
```

The server expects the resulting `.plan` files at the paths configured
in its YAML config (see Phase 4.3 — server RPC milestone for the
config schema).

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
