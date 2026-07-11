# tests/dashboard_api — proto → JSON presentation mapping.

add_executable(test_dashboard_json
    json_mapping_test.cpp
    event_cache_test.cpp
)

target_link_libraries(test_dashboard_json
    PRIVATE
        gate_dash_json
        Catch2::Catch2WithMain
)

catch_discover_tests(test_dashboard_json)

# The Subscribe consumer against a live in-process mock server — real
# gRPC streaming, reconnect, and since_event_id resume.
add_executable(test_dashboard_stream
    event_stream_test.cpp
)

target_link_libraries(test_dashboard_stream
    PRIVATE
        gate_dash_api
        Catch2::Catch2WithMain
)

catch_discover_tests(test_dashboard_stream
    PROPERTIES TIMEOUT 30
)

# 4.10.2 admin auth core — PBKDF2 credentials + JWT sessions. Links
# only gate_dash_auth: no Drogon, no gRPC.
add_executable(test_dashboard_auth
    auth_test.cpp
)

target_link_libraries(test_dashboard_auth
    PRIVATE
        gate_dash_auth
        Catch2::Catch2WithMain
)

catch_discover_tests(test_dashboard_auth
    PROPERTIES TIMEOUT 30
)
