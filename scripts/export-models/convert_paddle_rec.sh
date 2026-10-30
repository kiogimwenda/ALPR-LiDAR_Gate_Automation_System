#!/usr/bin/env bash
# Convert the fetched PaddleOCR rec checkpoints (PIR inference.json) to ONNX.
# paddle2onnx 2.x has no Python 3.13 wheel, so this uses a throwaway 3.12 venv:
#   python3.12 -m venv /tmp/p2o && /tmp/p2o/bin/pip install paddlepaddle==3.3.1 "paddle2onnx>=2.0" packaging onnx
#   P2O=/tmp/p2o/bin/paddle2onnx scripts/export-models/convert_paddle_rec.sh
# Output tensor is named fetch_name_0, [N, T, C] post-softmax, C = dict+blank+space
# (97 for en_PP-OCRv4, 438 en_PP-OCRv5, 838 latin_PP-OCRv5) — NOT the 37 the C++ needs.
set -euo pipefail
P2O="${P2O:-paddle2onnx}"
R="$(cd "$(dirname "$0")/../.." && pwd)/models/downloads/recognizers"
mkdir -p "$R/onnx"
for m in en_PP-OCRv4_mobile_rec en_PP-OCRv5_mobile_rec latin_PP-OCRv5_mobile_rec; do
    "$P2O" --model_dir "$R/$m" --model_filename inference.json --params_filename inference.pdiparams \
        --save_file "$R/onnx/$m.onnx" --opset_version 17
done
