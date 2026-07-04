# firmware/host/CMakeLists.txt — host-compiled mirrors of pure firmware
# modules so they can be unit-tested with Catch2 (gate_state_machine
# today; future pure modules join the list as they appear).
#
# This file is consumed by the top-level CMakeLists.txt — NOT by the
# ESP-IDF build, which has its own firmware/CMakeLists.txt and component
# discovery. Sources live under firmware/components/<name>/ so the
# ESP-IDF and host builds compile the exact same translation units.

add_library(gate_state_machine STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/gate_state_machine/src/state_machine.cpp
)

target_include_directories(gate_state_machine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/gate_state_machine/include
)

target_compile_features(gate_state_machine PUBLIC cxx_std_20)

# gRPC length-prefix framing codec from the gate_rpc component — the
# pure layer of the firmware's gRPC stack (ADR-011). Named
# gate_rpc_framing because `gate_rpc` is already the server-side RPC
# library target in server/rpc.
add_library(gate_rpc_framing STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/gate_rpc/src/grpc_framing.cpp
)

target_include_directories(gate_rpc_framing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/gate_rpc/include
)

target_compile_features(gate_rpc_framing PUBLIC cxx_std_20)
