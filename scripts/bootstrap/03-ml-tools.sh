#!/usr/bin/env bash
# 03-ml-tools.sh — Install ML/CV export tooling (PaddlePaddle, PaddleOCR, YOLOv9 repo).
# These are Python-side tools used for model export (PyTorch -> ONNX -> TRT).
# The actual inference runs in C++ via TensorRT.
set -euo pipefail

echo "=== ALPR+LiDAR Gate Automation — ML Tools Bootstrap ==="

# PaddlePaddle (CPU is sufficient for export; GPU inference is in C++ via TRT)
echo "[1/3] Installing PaddlePaddle and PaddleOCR..."
pip3 install --quiet paddlepaddle paddleocr

python3 -c "import paddle; print('  PaddlePaddle:', paddle.__version__)"
python3 -c "import paddleocr; print('  PaddleOCR: installed')"

# ONNX Runtime (GPU build for validation)
echo "[2/3] Checking ONNX Runtime..."
python3 -c "
import onnxruntime as ort
print('  ONNX Runtime:', ort.__version__)
print('  Providers:', ort.get_available_providers())
"

# YOLOv9 reference repo
echo "[3/3] Cloning YOLOv9 reference repo..."
YOLOV9_DIR="${HOME}/projects/yolov9"
if [ -d "$YOLOV9_DIR/.git" ]; then
    echo "  YOLOv9 already cloned at $YOLOV9_DIR"
    cd "$YOLOV9_DIR" && git pull --ff-only 2>/dev/null || echo "  (pull skipped — may have local changes)"
else
    git clone https://github.com/WongKinYiu/yolov9.git "$YOLOV9_DIR"
    echo "  YOLOv9 cloned to $YOLOV9_DIR"
fi

echo ""
echo "=== ML tools bootstrap complete ==="
