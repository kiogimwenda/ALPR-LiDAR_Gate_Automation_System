# tests/fusion — Catch2 unit tests for the verdict-ladder engine.

add_executable(test_fusion_engine fusion_engine_test.cpp)

target_link_libraries(test_fusion_engine
    PRIVATE
        gate_fusion
        Catch2::Catch2WithMain
)

target_compile_features(test_fusion_engine PRIVATE cxx_std_20)

catch_discover_tests(test_fusion_engine)
