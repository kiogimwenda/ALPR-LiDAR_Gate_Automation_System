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
