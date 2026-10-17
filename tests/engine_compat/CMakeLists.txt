# tests/engine_compat/CMakeLists.txt — GPU/TensorRT engine compatibility rules.
#
# engine_compat.hpp is header-only and standard-library-only, so these tests
# build in every configuration (including CPU-only CI) with no TensorRT.

add_executable(test_engine_compat engine_compat_test.cpp)
target_include_directories(test_engine_compat PRIVATE ${CMAKE_SOURCE_DIR}/server/inference/include)
target_link_libraries(test_engine_compat PRIVATE Catch2::Catch2WithMain)
target_compile_features(test_engine_compat PRIVATE cxx_std_20)
catch_discover_tests(test_engine_compat)
