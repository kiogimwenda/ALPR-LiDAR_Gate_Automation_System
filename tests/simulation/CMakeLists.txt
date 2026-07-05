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
