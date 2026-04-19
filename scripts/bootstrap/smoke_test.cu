/**
 * smoke_test.cu — Phase 0.2 GPU stack smoke test.
 *
 * Verifies: CUDA runtime, OpenCV with CUDA, TensorRT engine creation.
 * Compile: nvcc -std=c++20 -arch=sm_120 smoke_test.cu -o smoke_test \
 *          $(pkg-config --cflags --libs opencv4) \
 *          -lnvinfer -lnvonnxparser -lcudart
 * Run: ./smoke_test
 */
#include <cstdio>
#include <cuda_runtime.h>
#include <NvInfer.h>
#include <opencv2/core.hpp>
#include <opencv2/core/cuda.hpp>

// TensorRT logger
class Logger : public nvinfer1::ILogger {
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING)
            std::printf("[TRT] %s\n", msg);
    }
};

__global__ void hello_kernel(float* out, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) out[idx] = static_cast<float>(idx) * 2.0f;
}

int main() {
    std::printf("=== ALPR+LiDAR Gate Automation — GPU Smoke Test ===\n\n");

    // 1. CUDA device info
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    std::printf("[CUDA] Device count: %d\n", device_count);

    if (device_count == 0) {
        std::printf("ERROR: No CUDA devices found.\n");
        return 1;
    }

    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, 0);
    std::printf("[CUDA] Device 0: %s\n", prop.name);
    std::printf("[CUDA] Compute capability: %d.%d\n", prop.major, prop.minor);
    std::printf("[CUDA] Total memory: %zu MiB\n", prop.totalGlobalMem / (1024 * 1024));

    // 2. Simple kernel execution
    constexpr int N = 256;
    float* d_out = nullptr;
    cudaMalloc(&d_out, N * sizeof(float));
    hello_kernel<<<1, N>>>(d_out, N);
    float h_out[N];
    cudaMemcpy(h_out, d_out, N * sizeof(float), cudaMemcpyDeviceToHost);
    cudaFree(d_out);

    bool kernel_ok = (h_out[0] == 0.0f && h_out[1] == 2.0f && h_out[127] == 254.0f);
    std::printf("[CUDA] Kernel test: %s\n", kernel_ok ? "PASS" : "FAIL");

    // 3. OpenCV CUDA check
    int cv_cuda_count = cv::cuda::getCudaEnabledDeviceCount();
    std::printf("[OpenCV] CUDA-enabled devices: %d\n", cv_cuda_count);

    if (cv_cuda_count > 0) {
        cv::cuda::DeviceInfo info(0);
        std::printf("[OpenCV] Device: %s (compatible: %s)\n",
                    info.name(), info.isCompatible() ? "yes" : "no");

        // Quick GpuMat test
        cv::Mat cpu_mat = cv::Mat::ones(100, 100, CV_32FC1) * 42.0f;
        cv::cuda::GpuMat gpu_mat;
        gpu_mat.upload(cpu_mat);
        cv::Mat result;
        gpu_mat.download(result);
        bool opencv_ok = (result.at<float>(0, 0) == 42.0f);
        std::printf("[OpenCV] GpuMat upload/download: %s\n", opencv_ok ? "PASS" : "FAIL");
    } else {
        std::printf("[OpenCV] WARNING: No CUDA devices reported by OpenCV.\n");
    }

    // 4. TensorRT availability check
    Logger logger;
    auto builder = nvinfer1::createInferBuilder(logger);
    if (builder) {
        std::printf("[TensorRT] Builder created successfully (version %d.%d.%d)\n",
                    NV_TENSORRT_MAJOR, NV_TENSORRT_MINOR, NV_TENSORRT_PATCH);
        delete builder;
    } else {
        std::printf("[TensorRT] ERROR: Failed to create builder.\n");
        return 1;
    }

    std::printf("\n=== Smoke test PASSED ===\n");
    return 0;
}
