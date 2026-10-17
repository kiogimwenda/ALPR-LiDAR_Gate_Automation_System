#!/usr/bin/env bash
# build_engines.sh — build the ALPR TensorRT engines on THIS host's GPU.
#
# TensorRT engines are specific to the GPU generation and TensorRT version
# they are built with, and no TensorRT hardware-compatibility level spans
# Turing (RTX 20) and newer. The system supports every RTX generation from
# the 20 series (sm_75, sm_86, sm_89, sm_120), so engines are built where
# they run: on install, and again after a GPU swap or TensorRT upgrade.
#
# Usage:
#   scripts/export-models/build_engines.sh --onnx-dir <dir> [--out-dir /opt/gate/models] [--force]
#
# <dir> must contain the ONNX exports described in scripts/export-models/README.md:
#   yolov9_plate.onnx   (end2end, EfficientNMS_TRT, 1x3x640x640)
#   ppocrv4_rec.onnx    (input "x", 1x3x48xW, W in 32..320)
#
# Writes <out>/yolov9_plate.plan and <out>/ppocrv4_rec.plan, each with a
# .meta sidecar the C++ loader checks at startup. Up-to-date engines (same
# GPU compute capability and TensorRT version) are skipped unless --force.
# Uses scripts/export-models/convert_onnx_to_trt.py when TensorRT's Python
# bindings are installed, otherwise the trtexec CLI from libnvinfer-bin.

set -euo pipefail

ONNX_DIR="" OUT_DIR="/opt/gate/models" FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --onnx-dir) ONNX_DIR="$2"; shift 2 ;;
        --out-dir)  OUT_DIR="$2"; shift 2 ;;
        --force)    FORCE=1; shift ;;
        -h|--help)  sed -n '2,23p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ -n "${ONNX_DIR}" ]] || { echo "required: --onnx-dir <dir with the ONNX exports>" >&2; exit 2; }
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- this host's GPU -------------------------------------------------------
command -v nvidia-smi >/dev/null || { echo "nvidia-smi not found: no NVIDIA driver" >&2; exit 1; }
IFS=',' read -r GPU CC MEM < <(nvidia-smi --query-gpu=name,compute_cap,memory.total \
    --format=csv,noheader,nounits --id=0 | sed 's/, /,/g')
CC_MAJOR="${CC%%.*}" CC_MINOR="${CC##*.}"
echo "GPU: ${GPU} (sm_${CC_MAJOR}${CC_MINOR}, ${MEM} MiB)"
if (( CC_MAJOR < 7 || (CC_MAJOR == 7 && CC_MINOR < 5) )); then
    echo "error: ${GPU} is older than Turing — an NVIDIA RTX 20-series or newer GPU is required" >&2
    exit 1
fi
WORKSPACE_MB=$(( MEM / 2 < 4096 ? MEM / 2 : 4096 ))

# ---- TensorRT on this host -------------------------------------------------
if python3 -c 'import tensorrt' 2>/dev/null; then
    BUILDER=python
    TRT_VER=$(python3 -c "import tensorrt as t; print('.'.join(t.__version__.split('+')[0].split('.')[:3]))")
else
    command -v trtexec >/dev/null || {
        echo "error: neither TensorRT Python bindings nor trtexec found (apt install libnvinfer-bin)" >&2
        exit 1
    }
    BUILDER=trtexec
    TRT_VER=$(dpkg-query -W -f='${Version}' libnvinfer10 2>/dev/null | cut -d. -f1-3 | cut -d- -f1)
    [[ -n "${TRT_VER}" ]] || TRT_VER=$(trtexec --help 2>&1 | grep -oE 'TensorRT v[0-9]+' | head -1)
fi
echo "TensorRT: ${TRT_VER} (builder: ${BUILDER}), workspace ${WORKSPACE_MB} MiB"

install -d "${OUT_DIR}"

up_to_date() {  # plan is present and built for this GPU + TensorRT
    local meta="$1.meta"
    [[ -f "$1" && -f "${meta}" ]] || return 1
    grep -qx "compute_capability=${CC_MAJOR}.${CC_MINOR}" "${meta}" &&
        grep -qx "tensorrt=${TRT_VER}" "${meta}"
}

write_meta() {
    cat > "$1.meta" <<EOF
# written by build_engines.sh — checked by the C++ engine loader
gpu=${GPU}
compute_capability=${CC_MAJOR}.${CC_MINOR}
tensorrt=${TRT_VER}
hardware_compat=none
EOF
}

# build <onnx> <plan> [input-name min opt max]
build() {
    local onnx="$1" plan="$2" name="${3:-}" min="${4:-}" opt="${5:-}" max="${6:-}"
    [[ -f "${onnx}" ]] || { echo "error: missing ${onnx}" >&2; exit 1; }
    if (( ! FORCE )) && up_to_date "${plan}"; then
        echo "up to date: ${plan}"
        return
    fi
    echo "building ${plan} from ${onnx} …"
    if [[ ${BUILDER} == python ]]; then
        local profile=()
        [[ -n "${name}" ]] && profile=(--input-name "${name}" --min-shape "${min}" --opt-shape "${opt}" --max-shape "${max}")
        python3 "${HERE}/convert_onnx_to_trt.py" --onnx "${onnx}" --plan "${plan}" --fp16 \
            --workspace-mb "${WORKSPACE_MB}" --hardware-compat none "${profile[@]}"
    else
        local profile=()
        [[ -n "${name}" ]] && profile=("--minShapes=${name}:${min}" "--optShapes=${name}:${opt}" "--maxShapes=${name}:${max}")
        trtexec --onnx="${onnx}" --saveEngine="${plan}" --fp16 \
            --memPoolSize=workspace:"${WORKSPACE_MB}"M "${profile[@]}" --skipInference
    fi
    write_meta "${plan}"
    echo "built ${plan}"
}

build "${ONNX_DIR}/yolov9_plate.onnx" "${OUT_DIR}/yolov9_plate.plan"
build "${ONNX_DIR}/ppocrv4_rec.onnx"  "${OUT_DIR}/ppocrv4_rec.plan" x 1x3x48x32 1x3x48x160 1x3x48x320

echo "engines ready in ${OUT_DIR} for ${GPU} (sm_${CC_MAJOR}${CC_MINOR}, TensorRT ${TRT_VER})"
