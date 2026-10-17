# server/models/

Runtime assets the inference server loads at startup.

## What lives here

| File | Used by | Purpose |
|---|---|---|
| `dict_kenya_plates.txt` | `PaddleOcrRecognizer` | OCR character dictionary. One UTF-8 character per line. Index 0 in the model output is the CTC blank token; line `i` here corresponds to model class `i + 1`. |

## What does NOT live here

The TensorRT engine plans (`*.plan`, with their `.meta` sidecars) and ONNX
exports (`*.onnx`) are **not** committed. Engines are specific to the GPU
generation and TensorRT version they are built with, so they are built on
the machine that runs them — any NVIDIA RTX 20-series or newer GPU:

```bash
scripts/export-models/build_engines.sh --onnx-dir <dir with the ONNX exports> --out-dir server/models
```

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
