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
