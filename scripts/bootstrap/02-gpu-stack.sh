#!/usr/bin/env bash
# 02-gpu-stack.sh — Verify and install GPU stack (CUDA, cuDNN, TensorRT).
# IMPORTANT: The NVIDIA driver is installed on the WINDOWS side for WSL2.
#            Do NOT install nvidia-driver-* inside WSL2 — it breaks the passthrough.
# The CUDA toolkit, cuDNN, and TensorRT are installed inside WSL2.
set -euo pipefail

echo "=== ALPR+LiDAR Gate Automation — GPU Stack Bootstrap ==="

# Verify GPU is visible
echo "[1/5] Checking GPU visibility..."
if nvidia-smi > /dev/null 2>&1; then
    GPU_NAME=$(nvidia-smi --query-gpu=name --format=csv,noheader)
    DRIVER_VER=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader)
    VRAM=$(nvidia-smi --query-gpu=memory.total --format=csv,noheader)
    CC=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader --id=0)
    echo "  GPU:    $GPU_NAME (compute capability $CC)"
    echo "  Driver: $DRIVER_VER (Windows-side on WSL2)"
    echo "  VRAM:   $VRAM"
    # Supported: every NVIDIA RTX generation from the 20 series (Turing 7.5+).
    if [ "$(printf '%s\n7.5\n' "$CC" | sort -V | head -1)" != "7.5" ]; then
        echo "ERROR: $GPU_NAME (compute capability $CC) is older than Turing."
        echo "  This system needs an NVIDIA RTX 20-series or newer GPU."
        exit 1
    fi
    if [ "${DRIVER_VER%%.*}" -lt 580 ]; then
        echo "ERROR: driver $DRIVER_VER is too old — CUDA 13 needs driver >= 580."
        exit 1
    fi
else
    echo "ERROR: nvidia-smi not found or GPU not visible."
    echo "  - Ensure NVIDIA driver >= 580 is installed (on Windows for WSL2)"
    echo "  - Ensure WSL2 GPU passthrough is enabled"
    echo "  - Check /usr/lib/wsl/lib/ for libcuda.so"
    exit 1
fi

# Verify CUDA toolkit
echo "[2/5] Checking CUDA toolkit..."
if command -v nvcc &> /dev/null; then
    CUDA_VER=$(nvcc --version | grep "release" | sed 's/.*release //' | sed 's/,.*//')
    echo "  CUDA toolkit: $CUDA_VER"
else
    echo "ERROR: nvcc not found. Install CUDA toolkit from NVIDIA's WSL2-ubuntu repo."
    echo "  See: https://developer.nvidia.com/cuda-downloads?target_os=Linux&target_arch=x86_64&Distribution=WSL-Ubuntu"
    exit 1
fi

# Verify architecture support
echo "[3/5] Verifying compute architecture support..."
TEST_CU=$(mktemp /tmp/sm_test_XXXXXX.cu)
cat > "$TEST_CU" << 'CUDA_EOF'
#include <cstdio>
__global__ void k() {}
int main() {
    k<<<1,1>>>();
    // A kernel compiled for another architecture fails at launch
    // ("no kernel image is available") — only a clean launch counts as run.
    cudaError_t e = cudaGetLastError();
    if (e == cudaSuccess) e = cudaDeviceSynchronize();
    if (e != cudaSuccess) { printf("launch_failed: %s\n", cudaGetErrorString(e)); return 1; }
    printf("arch_test_ok\n");
    return 0;
}
CUDA_EOF

# Every supported RTX generation: 20 (Turing), 30 (Ampere), 40 (Ada), 50 (Blackwell).
ARCH_FAILED=0
for ARCH in sm_75 sm_86 sm_89 sm_120; do
    if nvcc -arch="$ARCH" "$TEST_CU" -o /tmp/sm_test_bin 2>/dev/null && /tmp/sm_test_bin 2>/dev/null | grep -q "arch_test_ok"; then
        echo "  $ARCH: OK (compile + run)"
    elif nvcc -arch="$ARCH" "$TEST_CU" -o /tmp/sm_test_bin 2>/dev/null; then
        echo "  $ARCH: OK (compile only — not the current GPU)"
    else
        echo "  $ARCH: FAILED"
        ARCH_FAILED=1
    fi
done
rm -f "$TEST_CU" /tmp/sm_test_bin
if [ "$ARCH_FAILED" -ne 0 ]; then
    echo "ERROR: the CUDA toolkit cannot target every supported RTX generation."
    exit 1
fi

# Verify cuDNN
echo "[4/5] Checking cuDNN..."
# (dpkg-query, not `dpkg -l | grep -q`: under pipefail grep -q's early
# exit SIGPIPEs dpkg and the check wrongly reports "not found")
CUDNN_PKG=$(dpkg-query -W -f='${Package} ${Version}\n' 'libcudnn9-dev-*' 2>/dev/null | head -1)
if [ -n "$CUDNN_PKG" ]; then
    echo "  cuDNN: $CUDNN_PKG"
else
    echo "WARNING: cuDNN dev package not found. Install libcudnn9-dev-cuda-13"
fi

# Verify TensorRT
echo "[5/5] Checking TensorRT..."
TRT_VER=$(dpkg-query -W -f='${Version}' libnvinfer-dev 2>/dev/null || true)
if [ -n "$TRT_VER" ]; then
    echo "  TensorRT: $TRT_VER"
    if command -v trtexec &> /dev/null; then
        echo "  trtexec: $(which trtexec)"
    else
        echo "  trtexec: NOT FOUND — install libnvinfer-bin"
    fi
else
    echo "WARNING: TensorRT dev package not found. Install tensorrt-dev"
fi

echo ""
echo "=== GPU stack check complete ==="
