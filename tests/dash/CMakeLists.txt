# tests/dash — Catch2 unit tests for the event broadcaster fan-out.

add_executable(test_dash_event_broadcaster event_broadcaster_test.cpp)

target_link_libraries(test_dash_event_broadcaster
    PRIVATE
        gate_dash
        Catch2::Catch2WithMain
)

target_compile_features(test_dash_event_broadcaster PRIVATE cxx_std_20)

catch_discover_tests(test_dash_event_broadcaster)
