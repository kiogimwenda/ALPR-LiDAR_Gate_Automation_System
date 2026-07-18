# tests/vision — scenario ingest, frame building, submit-client config.

add_executable(test_vision
    scenario_source_test.cpp
    frame_builder_test.cpp
    submit_client_test.cpp
)

target_link_libraries(test_vision
    PRIVATE
        gate_vision
        Catch2::Catch2WithMain
)

target_compile_features(test_vision PRIVATE cxx_std_20)

catch_discover_tests(test_vision)
