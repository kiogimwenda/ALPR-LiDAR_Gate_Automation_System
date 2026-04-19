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
    echo "  GPU:    $GPU_NAME"
    echo "  Driver: $DRIVER_VER (Windows-side)"
    echo "  VRAM:   $VRAM"
else
    echo "ERROR: nvidia-smi not found or GPU not visible."
    echo "  - Ensure NVIDIA driver >= 595.x is installed on Windows"
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
    cudaDeviceSynchronize();
    printf("arch_test_ok\n");
    return 0;
}
CUDA_EOF

for ARCH in sm_120 sm_89; do
    if nvcc -arch="$ARCH" "$TEST_CU" -o /tmp/sm_test_bin 2>/dev/null && /tmp/sm_test_bin 2>/dev/null | grep -q "arch_test_ok"; then
        echo "  $ARCH: OK (compile + run)"
    elif nvcc -arch="$ARCH" "$TEST_CU" -o /tmp/sm_test_bin 2>/dev/null; then
        echo "  $ARCH: OK (compile only — not the current GPU)"
    else
        echo "  $ARCH: FAILED"
    fi
done
rm -f "$TEST_CU" /tmp/sm_test_bin

# Verify cuDNN
echo "[4/5] Checking cuDNN..."
if dpkg -l | grep -q "libcudnn9-dev"; then
    CUDNN_VER=$(dpkg -l | grep "libcudnn9-dev" | awk '{print $3}')
    echo "  cuDNN: $CUDNN_VER"
else
    echo "WARNING: cuDNN dev package not found. Install libcudnn9-dev-cuda-12"
fi

# Verify TensorRT
echo "[5/5] Checking TensorRT..."
if dpkg -l | grep -q "libnvinfer-dev"; then
    TRT_VER=$(dpkg -l | grep "libnvinfer-dev " | awk '{print $3}')
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
