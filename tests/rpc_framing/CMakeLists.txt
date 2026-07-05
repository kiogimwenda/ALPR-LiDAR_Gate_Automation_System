# tests/rpc_framing — gRPC length-prefix framing codec tests.

add_executable(test_rpc_framing
    rpc_framing_test.cpp
    command_tracker_test.cpp
)

target_link_libraries(test_rpc_framing
    PRIVATE
        gate_rpc_framing
        Catch2::Catch2WithMain
)

catch_discover_tests(test_rpc_framing)
