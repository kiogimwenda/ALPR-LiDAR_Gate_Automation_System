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
)

target_include_directories(gate_vision
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(gate_vision
    PUBLIC
        gate_proto
    PRIVATE
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

target_compile_features(gate_vision PUBLIC cxx_std_20)

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
target_compile_features(gate-vision PRIVATE cxx_std_20)
