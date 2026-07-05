# tests/dashboard_api — proto → JSON presentation mapping.

add_executable(test_dashboard_json
    json_mapping_test.cpp
)

target_link_libraries(test_dashboard_json
    PRIVATE
        gate_dash_json
        Catch2::Catch2WithMain
)

catch_discover_tests(test_dashboard_json)
