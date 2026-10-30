#!/usr/bin/env bash
# prepare_onnx.sh — produce the two deployed ALPR ONNX models from pinned upstream files.
#
#   1. fetch + SHA-256-verify the upstream artifacts       (fetch_models.py --only prod)
#   2. graft EfficientNMS_TRT onto the YOLOv9-s-608 export (graft_efficientnms.py)
#   3. stage the recognizer ONNX and its plate config
#
# Usage:
#   scripts/export-models/prepare_onnx.sh [--out-dir models/weights]
# then
#   scripts/export-models/build_engines.sh --onnx-dir models/weights --out-dir <engine dir>
#
# Output (git-ignored):
#   yolov9_plate.onnx           open-image-models YOLOv9-s-608 + EfficientNMS_TRT, 1x3x608x608
#   plate_ocr.onnx              fast-plate-ocr cct-s-v2-global, uint8 NHWC 1x64x128x3
#   plate_ocr_config.yaml       its alphabet / pad char / input size
# Needs: python3 with onnx (the ML tooling env), network access to GitHub.

set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../.." && pwd)"
OUT="${REPO}/models/weights"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT="$2"; shift 2 ;;
        -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
DL="${REPO}/models/downloads"

python3 -I "${HERE}/fetch_models.py" --only prod
install -d "${OUT}"
python3 -I "${HERE}/graft_efficientnms.py" \
    --onnx "${DL}/detectors/oim-yolov9-s-608/yolo-v9-s-608-license-plates-end2end.onnx" \
    --out "${OUT}/yolov9_plate.onnx" --score 0.10 --iou 0.45 --topk 100
install -m 0644 "${DL}/recognizers/fpo-cct-s-v2-global/cct_s_v2_global.onnx" "${OUT}/plate_ocr.onnx"
install -m 0644 "${DL}/recognizers/fpo-cct-s-v2-global/cct_s_v2_global_plate_config.yaml" \
    "${OUT}/plate_ocr_config.yaml"

# Pinned results: the graft is deterministic, so its output hash is fixed.
EXPECT_DET=55ac0d589c9b4e5d96bdbbb9d1cde89d1a6c52e42c3a5ae768093e41f3a5ac71
got=$(sha256sum "${OUT}/yolov9_plate.onnx" | cut -d' ' -f1)
if [[ "${got}" != "${EXPECT_DET}" ]]; then
    echo "warning: yolov9_plate.onnx sha256 ${got} != pinned ${EXPECT_DET} (onnx version change?)" >&2
fi
sha256sum "${OUT}"/yolov9_plate.onnx "${OUT}"/plate_ocr.onnx "${OUT}"/plate_ocr_config.yaml
echo "ONNX models ready in ${OUT}"
