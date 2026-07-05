# simulation/ — gate-sim, the virtual gate field controller (Phase 4.6).
#
# gate_sim_core carries everything testable (VirtualGate physics +
# SimClient session); the gate-sim executable is a thin CLI wrapper.
# Added by the root CMakeLists only when gate_proto and CLI11 resolved
# (vcpkg toolchain), alongside the firmware/host pure mirrors.

add_library(gate_sim_core STATIC
    src/virtual_gate.cpp
    src/sim_client.cpp
)

target_include_directories(gate_sim_core PUBLIC include)

target_link_libraries(gate_sim_core
    PUBLIC
        gate_proto          # grpc++ stubs (carries gRPC/protobuf publicly)
        gate_rpc_framing    # CommandTracker (links gate_state_machine)
)

target_compile_features(gate_sim_core PUBLIC cxx_std_20)

add_executable(gate-sim src/main.cpp)

target_link_libraries(gate-sim
    PRIVATE
        gate_sim_core
        CLI11::CLI11
)
