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
