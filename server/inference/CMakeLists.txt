# server/inference — TensorRT-backed inference primitives.
#
# Builds a single static library `gate_inference` that wraps TensorRT, exposes
# RAII engine handles, and (later in Phase 4.2) hosts the YOLOv9 plate detector,
# PaddleOCR recognizer, and ALPR pipeline orchestrator.
#
# Dependencies that must already be resolved by the parent scope:
#   * CUDAToolkit::cudart   — set up by `find_package(CUDAToolkit)` in root CMake
#   * spdlog::spdlog        — vcpkg
#   * OpenCV (core+imgproc) — system install (source build at /usr/local)
#   * TensorRT (libnvinfer) — system install; we locate it ourselves below.

# --- OpenCV -----------------------------------------------------------------
# Built from source against CUDA — see scripts/bootstrap/03-opencv-cuda.sh.
# The source build installs an OpenCVConfig.cmake; we only need core + imgproc
# at the inference layer (no GUI / no videoio).
#
# Wrinkle: because OpenCV was built with CUDA support, its config calls the
# legacy FindCUDA module unconditionally (`find_host_package(CUDA REQUIRED)`).
# CMake 3.27+ defaulted policy CMP0146 to NEW, which removes that module.
# Policy pushes don't propagate through vcpkg's find_package wrapper, so we
# pre-populate the variables OpenCVConfig.cmake actually reads (sourced from
# the modern CUDA::* imported targets). The `if(NOT CUDA_FOUND)` check in
# OpenCV's config then skips the legacy lookup entirely.
if(NOT CUDA_FOUND)
    set(CUDA_FOUND          TRUE)
    set(CUDA_VERSION_STRING "${CUDAToolkit_VERSION_MAJOR}.${CUDAToolkit_VERSION_MINOR}")
    set(CUDA_VERSION        "${CUDA_VERSION_STRING}")
    get_target_property(_cudart_lib  CUDA::cudart   IMPORTED_LOCATION)
    get_target_property(_nppc_lib    CUDA::nppc     IMPORTED_LOCATION)
    get_target_property(_nppial_lib  CUDA::nppial   IMPORTED_LOCATION)
    get_target_property(_npps_lib    CUDA::npps     IMPORTED_LOCATION)
    get_target_property(_cublas_lib  CUDA::cublas   IMPORTED_LOCATION)
    get_target_property(_cufft_lib   CUDA::cufft    IMPORTED_LOCATION)
    set(CUDA_LIBRARIES        "${_cudart_lib}")
    set(CUDA_nppc_LIBRARY     "${_nppc_lib}")
    set(CUDA_nppi_LIBRARY     "${_nppial_lib}")
    set(CUDA_npps_LIBRARY     "${_npps_lib}")
    set(CUDA_CUBLAS_LIBRARIES "${_cublas_lib}")
    set(CUDA_CUFFT_LIBRARIES  "${_cufft_lib}")
    # OpenCV's config also calls FindCUDA's helper macro find_cuda_helper_libs
    # for nppc/nppi/npps. Since we never loaded FindCUDA, the macro is unknown.
    # Provide a no-op shim — the libraries are already populated above.
    if(NOT COMMAND find_cuda_helper_libs)
        macro(find_cuda_helper_libs)
        endmacro()
    endif()
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc)
message(STATUS "Found OpenCV ${OpenCV_VERSION} (components: core, imgproc)")

# --- TensorRT discovery -----------------------------------------------------
# TensorRT does not ship a CMake config. We locate it through standard system
# paths (Debian package layout) plus an optional TENSORRT_ROOT override, then
# expose it as an INTERFACE target the rest of the project can link to.

find_path(TENSORRT_INCLUDE_DIR
    NAMES NvInferRuntime.h
    HINTS ${TENSORRT_ROOT} $ENV{TENSORRT_ROOT}
    PATH_SUFFIXES include include/x86_64-linux-gnu
    PATHS /usr /usr/local
    DOC "Directory containing NvInferRuntime.h"
)

find_library(TENSORRT_NVINFER_LIB
    NAMES nvinfer
    HINTS ${TENSORRT_ROOT} $ENV{TENSORRT_ROOT}
    PATH_SUFFIXES lib lib/x86_64-linux-gnu
    PATHS /usr /usr/local
    DOC "Path to libnvinfer.so"
)

if(NOT TENSORRT_INCLUDE_DIR OR NOT TENSORRT_NVINFER_LIB)
    message(FATAL_ERROR
        "server/inference: TensorRT not found. Install libnvinfer-dev "
        "or set -DTENSORRT_ROOT=/path/to/tensorrt. "
        "Headers searched for: NvInferRuntime.h. Library searched for: nvinfer.")
endif()

message(STATUS "Found TensorRT: ${TENSORRT_NVINFER_LIB} (include: ${TENSORRT_INCLUDE_DIR})")

add_library(TensorRT::nvinfer UNKNOWN IMPORTED)
set_target_properties(TensorRT::nvinfer PROPERTIES
    IMPORTED_LOCATION             "${TENSORRT_NVINFER_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${TENSORRT_INCLUDE_DIR}"
)

# --- gate_inference library -------------------------------------------------

add_library(gate_inference STATIC
    src/trt_engine.cpp
    src/yolo_plate_detector.cpp
    src/paddle_ocr_recognizer.cpp
    src/alpr_pipeline.cpp
)

target_include_directories(gate_inference
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(gate_inference
    PUBLIC
        TensorRT::nvinfer
        CUDA::cudart
        opencv_core
        opencv_imgproc
    PRIVATE
        spdlog::spdlog
)

target_compile_features(gate_inference PUBLIC cxx_std_20)

# Headers from TensorRT trip -Wpedantic and -Wshadow style warnings; we keep
# our own code at -Werror but mark the third-party include path as SYSTEM so
# it's exempt.
target_include_directories(gate_inference SYSTEM PUBLIC ${TENSORRT_INCLUDE_DIR})
