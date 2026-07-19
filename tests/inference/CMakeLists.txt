# tests/inference/CMakeLists.txt — host-side tests for the inference layer.
#
# The tests link against the production gate_inference static library so they
# exercise the same code path the server runs through. They cover only the
# CPU-only host algorithms (letterbox, CTC decode, crop expansion) — TRT
# integration tests are a Phase 4.9 concern.
#
# This subdirectory is only included when gate_inference exists, which is the
# case for any build that has BUILD_SERVER=ON (see tests/CMakeLists.txt).

add_executable(test_inference_cpu_algorithms
    cpu_algorithms_test.cpp
)

target_link_libraries(test_inference_cpu_algorithms
    PRIVATE
        gate_inference
        Catch2::Catch2WithMain
)

target_compile_features(test_inference_cpu_algorithms PRIVATE cxx_std_20)

catch_discover_tests(test_inference_cpu_algorithms)
