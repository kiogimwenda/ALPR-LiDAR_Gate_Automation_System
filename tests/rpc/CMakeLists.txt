# tests/rpc — Unit tests for the gRPC service implementations.
#
# These exercise handlers directly (no network round-trip) plus a small
# server-lifecycle test that opens an ephemeral port. Full client/server
# integration over a channel lives in tests/integration (4.3.6).

add_executable(test_rpc_admin admin_service_test.cpp)
target_link_libraries(test_rpc_admin PRIVATE gate_rpc Catch2::Catch2WithMain)
target_compile_features(test_rpc_admin PRIVATE cxx_std_20)
catch_discover_tests(test_rpc_admin)

add_executable(test_rpc_dashboard dashboard_service_test.cpp)
target_link_libraries(test_rpc_dashboard PRIVATE gate_rpc Catch2::Catch2WithMain)
target_compile_features(test_rpc_dashboard PRIVATE cxx_std_20)
catch_discover_tests(test_rpc_dashboard)

add_executable(test_rpc_server server_lifecycle_test.cpp)
target_link_libraries(test_rpc_server PRIVATE gate_rpc Catch2::Catch2WithMain)
target_compile_features(test_rpc_server PRIVATE cxx_std_20)
catch_discover_tests(test_rpc_server)
