# tests/inference/CMakeLists.txt — host-side tests for the inference layer.
#
# test_inference_plate_text links gate_plate_text (decoders, plate config,
# Kenya normalisation): plain C++, built and run in every configuration,
# including the CPU-only CI build.
#
# test_inference_cpu_algorithms links the production gate_inference library
# (OpenCV + TensorRT) and covers the CPU-only image paths — letterbox, crop
# expansion, CTC decode, recognizer preprocessing. It exists only in GPU
# builds (gate_inference needs ENABLE_GPU); no test here touches a GPU.

if(TARGET gate_plate_text)
    add_executable(test_inference_plate_text plate_text_test.cpp)
    target_link_libraries(test_inference_plate_text
        PRIVATE gate_plate_text Catch2::Catch2WithMain)
    target_compile_features(test_inference_plate_text PRIVATE cxx_std_20)
    catch_discover_tests(test_inference_plate_text)
endif()

if(TARGET gate_inference)
    add_executable(test_inference_cpu_algorithms cpu_algorithms_test.cpp)
    target_link_libraries(test_inference_cpu_algorithms
        PRIVATE gate_inference Catch2::Catch2WithMain)
    target_compile_features(test_inference_cpu_algorithms PRIVATE cxx_std_20)
    catch_discover_tests(test_inference_cpu_algorithms)
endif()
