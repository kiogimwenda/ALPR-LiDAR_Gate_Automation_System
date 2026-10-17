# server/vision — sensor-agnostic detection ingest (Phase 5.2).
#
# `gate_vision` is a small static library the gate-vision daemon (next
# milestone) composes: ObservationSource seam, scripted ScenarioSource
# (nlohmann-json, fail-closed parsing), FrameBuilder (observations →
# DetectionFrame) and SubmitClient (SubmitDetection over grpc++ with
# the Phase 4.10.1 TLS ladder). Deliberately CPU-only — no TensorRT,
# OpenCV, or CUDA; the GPU camera source plugs into the seam later.
# Depends on `gate_proto` (which carries gRPC/protobuf publicly) plus
# vcpkg's nlohmann-json and spdlog.

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(gate_vision STATIC
    src/scenario_source.cpp
    src/frame_builder.cpp
    src/submit_client.cpp
    src/fused_source.cpp
    # LiDAR path (Phase 6.1) — CPU-only, no PCL/Eigen.
    src/lidar/vehicle_detector.cpp
    src/lidar/lidar_config.cpp
    src/lidar/lidar_worker.cpp
    src/lidar/pcd_source.cpp
    src/lidar/synthetic_source.cpp
)

target_include_directories(gate_vision
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

find_package(Threads REQUIRED)

target_link_libraries(gate_vision
    PUBLIC
        gate_proto
        Threads::Threads
    PRIVATE
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

target_compile_features(gate_vision PUBLIC cxx_std_20)

# --- Unitree L1 driver (optional) -------------------------------------------
# Unitree's unilidar_sdk ships a prebuilt static library (BSD-3-Clause).
# Point UNITREE_LIDAR_SDK_DIR at its unitree_lidar_sdk/ folder (the one
# holding include/ and lib/<arch>/) to build gate-vision with
# `--lidar unitree-serial:…` / `unitree-udp:…` support:
#   git clone https://github.com/unitreerobotics/unilidar_sdk
#   cmake … -DUNITREE_LIDAR_SDK_DIR=$PWD/unilidar_sdk/unitree_lidar_sdk
set(UNITREE_LIDAR_SDK_DIR "" CACHE PATH "unilidar_sdk/unitree_lidar_sdk directory (optional)")
if(UNITREE_LIDAR_SDK_DIR)
    find_path(UNITREE_LIDAR_INCLUDE unitree_lidar_sdk.h
        PATHS ${UNITREE_LIDAR_SDK_DIR}/include NO_DEFAULT_PATH REQUIRED)
    find_library(UNITREE_LIDAR_LIB unitree_lidar_sdk
        PATHS ${UNITREE_LIDAR_SDK_DIR}/lib/${CMAKE_SYSTEM_PROCESSOR} NO_DEFAULT_PATH REQUIRED)
    add_library(gate_vision_unitree STATIC src/lidar/unitree_l1_source.cpp)
    # SYSTEM: the vendor header is not -Wall/-Werror clean.
    target_include_directories(gate_vision_unitree SYSTEM PRIVATE ${UNITREE_LIDAR_INCLUDE})
    target_link_libraries(gate_vision_unitree
        PUBLIC gate_vision
        PRIVATE ${UNITREE_LIDAR_LIB} spdlog::spdlog)
    target_compile_features(gate_vision_unitree PUBLIC cxx_std_20)
    message(STATUS "gate-vision: Unitree L1 driver enabled (${UNITREE_LIDAR_LIB})")
endif()

# --- lidar-bench --------------------------------------------------------------
# Measures the detector against the latency requirement (docs/lidar.md)
# on ray-cast scenes: `lidar-bench [--points-per-second N] [--frames N]`.
add_executable(lidar-bench tools/lidar_bench.cpp)
target_link_libraries(lidar-bench PRIVATE gate_vision spdlog::spdlog)
target_compile_features(lidar-bench PRIVATE cxx_std_20)

# --- camera source (GPU builds only) ----------------------------------------
# The first real sensor behind the seam: OpenCV VideoCapture → TensorRT
# AlprPipeline. Needs videoio on top of inference's core+imgproc, so it
# re-runs the OpenCV find (with the shared CUDA shim) in this scope.
if(ENABLE_GPU AND TARGET gate_inference)
    include(${CMAKE_SOURCE_DIR}/cmake/opencv-cuda-shim.cmake)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

    add_library(gate_vision_camera STATIC
        src/camera_source.cpp
    )
    target_link_libraries(gate_vision_camera
        PUBLIC
            gate_vision
            gate_inference
            opencv_videoio
        PRIVATE
            spdlog::spdlog
    )
    target_compile_features(gate_vision_camera PUBLIC cxx_std_20)
endif()

# --- gate-vision daemon -----------------------------------------------------
# Composition root: one source (scenario always; camera when the GPU
# stack is present), FrameBuilder, SubmitClient. CPU builds produce a
# fully functional scenario-driven daemon — that's what the E2E runs.
find_package(CLI11 CONFIG REQUIRED)

add_executable(gate-vision src/gate_vision_main.cpp)
target_link_libraries(gate-vision
    PRIVATE
        gate_vision
        CLI11::CLI11
        spdlog::spdlog
)
if(TARGET gate_vision_camera)
    target_link_libraries(gate-vision PRIVATE gate_vision_camera)
    target_compile_definitions(gate-vision PRIVATE GATE_VISION_HAVE_CAMERA=1)
endif()
if(TARGET gate_vision_unitree)
    target_link_libraries(gate-vision PRIVATE gate_vision_unitree)
    target_compile_definitions(gate-vision PRIVATE GATE_VISION_HAVE_UNITREE=1)
endif()
target_compile_features(gate-vision PRIVATE cxx_std_20)
