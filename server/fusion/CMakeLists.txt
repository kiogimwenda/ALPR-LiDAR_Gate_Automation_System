# server/fusion — ALPR + LiDAR decision fusion engine.
#
# `gate_fusion` is a small static library implementing the verdict ladder
# from docs/diagrams/04-fusion-decision.mmd. It depends only on `gate_proto`
# (for AuthorizeRequest / AuthDecision) and `gate_auth` (for the allowlist
# store and the is_allowed_now() helper). No TensorRT, OpenCV, or CUDA.

add_library(gate_fusion STATIC
    src/fusion_engine.cpp
)

target_include_directories(gate_fusion
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(gate_fusion
    PUBLIC
        gate_proto
        gate_auth
)

target_compile_features(gate_fusion PUBLIC cxx_std_20)
