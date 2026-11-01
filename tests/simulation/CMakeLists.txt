# tests/simulation — gate-sim ↔ mock server roundtrip over real gRPC.

add_executable(test_sim_roundtrip
    sim_roundtrip_test.cpp
)

target_link_libraries(test_sim_roundtrip
    PRIVATE
        gate_sim_core
        Catch2::Catch2WithMain
)

# Each case runs a live gRPC server + streaming session with real
# clocks; give discovery/execution generous room.
catch_discover_tests(test_sim_roundtrip
    PROPERTIES TIMEOUT 30
)

# Pure physics: no network, simulated time.
add_executable(test_virtual_gate
    virtual_gate_test.cpp
)

target_link_libraries(test_virtual_gate
    PRIVATE
        gate_sim_core
        Catch2::Catch2WithMain
)

catch_discover_tests(test_virtual_gate)

# In-process end to end: firmware core + virtual operator + real dispatcher.
add_executable(test_v2_stack
    v2_stack_test.cpp
)

target_link_libraries(test_v2_stack
    PRIVATE
        gate_sim_core
        gate_rpc
        Catch2::Catch2WithMain
)

catch_discover_tests(test_v2_stack)
