# ADR-010: Model Licensing — YOLOv9 GPL-3.0 Implications

## Status
Accepted

## Date
2026-04-19

## Context

YOLOv9 is released under **GPL-3.0**, a strong copyleft license. This project uses YOLOv9 for license plate detection. The question is whether our system constitutes a "derivative work" of YOLOv9, which would require the entire system to be GPL-3.0.

### How We Use YOLOv9

The YOLOv9 codebase (Python/PyTorch) is used in **two ways**:

1. **Training/export tooling** (development-time only):
   - Fine-tune YOLOv9 on Kenyan plate detection dataset
   - Export trained model: PyTorch → ONNX → TensorRT engine
   - This step uses the GPL-3.0 Python code directly

2. **Runtime inference** (production):
   - Load the exported TensorRT engine (binary blob, `.engine` file)
   - Run inference via the TensorRT C++ API
   - **No YOLOv9 Python code is executed at runtime**
   - The C++ inference code calls TensorRT APIs, not YOLOv9 code

### Legal Analysis

The key question: **Is a TensorRT engine file a derivative work of the YOLOv9 source code?**

Arguments that it **is** a derivative work:
- The engine encodes the trained weights and architecture, which were defined by YOLOv9
- The FSF's position is broad: "output of a program can be a derivative work of the program"
- GPL-3.0 Section 0 defines "modify" broadly

Arguments that it **is not** a derivative work:
- The engine is a mathematical model (learned weights), not source code
- The TensorRT engine format is NVIDIA's proprietary runtime — it contains no YOLOv9 source
- Precedent: using GCC (GPL) to compile code doesn't make the output GPL
- The ONNX intermediate format is an open standard, not YOLOv9-specific

### Options

1. **License entire project GPL-3.0** (current choice, ADR-000)
   - Safe: no license risk
   - Source must be shared with anyone who receives binaries
   - Commercial deployment still allowed; customers can request source

2. **Dual-license: GPL-3.0 for training tools, MIT/Apache for runtime**
   - Legally defensible if TRT engine is not a derivative work
   - More permissive for commercial users of the runtime
   - Requires clean separation of GPL and non-GPL code

3. **Use Ultralytics YOLOv8 with AGPL-3.0 commercial license**
   - Ultralytics offers paid licenses for YOLOv8 that remove copyleft
   - ~USD 1,000-2,000/year
   - But YOLOv9 is a different project (WongKinYiu), not Ultralytics

4. **Replace YOLOv9 with a permissively-licensed detector**
   - RT-DETR (Apache 2.0), EfficientDet (Apache 2.0), YOLOX (Apache 2.0)
   - Eliminates GPL concern entirely
   - May require more training effort to match YOLOv9 accuracy on plates

## Decision

**For now: GPL-3.0 for the entire project** (per ADR-000). This is the safe default.

**Future path:** If commercial licensing becomes a requirement, evaluate:
1. First: replace YOLOv9 with RT-DETR or YOLOX (Apache 2.0) — this is the cleanest solution
2. Alternatively: dual-license with legal review confirming TRT engine separation
3. Last resort: contact WongKinYiu about commercial licensing terms for YOLOv9

The model architecture is encapsulated behind the TRT engine loader — swapping the detection model requires only re-exporting a new ONNX model and regenerating the TRT engine, no C++ code changes.

## Consequences

- Project remains GPL-3.0 until a commercial licensing need arises
- Detection model is treated as a swappable component behind the TRT engine interface
- Export scripts in `scripts/export-models/` clearly document which model they export and its license
- If a permissively-licensed detector achieves comparable accuracy on Kenyan plates, migration is straightforward
- Training/export tooling (Python) is separate from runtime (C++) — clean boundary exists
