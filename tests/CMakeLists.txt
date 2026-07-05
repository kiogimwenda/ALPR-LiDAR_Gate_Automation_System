# tests/CMakeLists.txt — top-level test aggregator.

find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

# Proto contract tests link against gate_proto (built by shared/proto/ when
# BUILD_PROTO=ON and Protobuf+gRPC are resolved). Gate by target presence so
# a tests-only build with BUILD_PROTO=OFF doesn't try to compile against a
# library that was never built.
if(TARGET gate_proto)
    add_subdirectory(proto)
endif()

# Inference tests link against gate_inference (which only exists when
# BUILD_SERVER=ON pulled in server/inference). We gate by target presence so
# CI's GPU-OFF builds skip the inference tests cleanly without having to
# guess at flags.
if(TARGET gate_inference)
    add_subdirectory(inference)
endif()

# Auth tests link against gate_auth (which transitively requires gate_proto).
# Gated the same way so a CPU-only proto build still gets full coverage of
# the allowlist/blocklist store without paying for TensorRT.
if(TARGET gate_auth)
    add_subdirectory(auth)
endif()

# Fusion-engine tests link against gate_fusion (which transitively requires
# gate_proto and gate_auth). Same gating model — CPU-only proto builds get
# the full verdict-ladder coverage with no TensorRT cost.
if(TARGET gate_fusion)
    add_subdirectory(fusion)
endif()

# Dashboard broadcaster tests — gate_dash needs only gate_proto.
if(TARGET gate_dash)
    add_subdirectory(dash)
endif()

# RPC service handler + lifecycle tests.
if(TARGET gate_rpc)
    add_subdirectory(rpc)
endif()

# End-to-end integration tests over real gRPC channels.
if(TARGET gate_rpc)
    add_subdirectory(integration)
endif()

# Firmware state machine tests link against the host-compiled mirror
# (declared in firmware/host/CMakeLists.txt). No gRPC / GPU / IDF
# dependencies — these run in every CI configuration.
if(TARGET gate_state_machine)
    add_subdirectory(state_machine)
endif()

# gRPC framing codec tests — the pure layer of the firmware's gRPC
# stack (ADR-011), mirrored to host the same way.
if(TARGET gate_rpc_framing)
    add_subdirectory(rpc_framing)
endif()

# OTA manifest accept/reject rules (ADR-009), same mirror pattern.
if(TARGET gate_ota_manifest)
    add_subdirectory(ota_manifest)
endif()

# End-to-end scenarios over the composed pure firmware modules
# (Phase 4.5.6): state machine + command tracker + the GateController
# policy wiring, replayed against a simulated gate.
if(TARGET gate_rpc_framing)
    add_subdirectory(firmware_integration)
endif()

# gate-sim ↔ mock server roundtrip over real gRPC (Phase 4.6).
if(TARGET gate_sim_core)
    add_subdirectory(simulation)
endif()
