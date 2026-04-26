# server/ — GPU inference + RPC server.
#
# This subdirectory is added by the root build only when:
#   * BUILD_SERVER = ON
#   * spdlog and (later) Drogon are resolved by vcpkg
#   * CUDA and TensorRT are available
#
# Subsequent Phase 4 milestones will add `rpc/` (gRPC service host) and `app/`
# (the daemon entry point) alongside `inference/`.

add_subdirectory(inference)

# auth/ depends on gate_proto (built in shared/proto when BUILD_PROTO=ON).
# Skip cleanly if the proto target is unavailable so a CUDA-only configure
# doesn't error out — the fusion engine and admin service won't link
# without it, and the parent already gates BUILD_SERVER appropriately.
if(TARGET gate_proto)
    add_subdirectory(auth)
endif()

# fusion/ depends on both gate_proto and gate_auth — the verdict ladder needs
# the proto types and the allowlist store. Same gating rationale as auth/.
if(TARGET gate_auth)
    add_subdirectory(fusion)
endif()

# dash/ depends only on gate_proto. Same gating model.
if(TARGET gate_proto)
    add_subdirectory(dash)
endif()

# rpc/ depends on gate_proto + gate_auth + gate_fusion + gate_dash.
# Gate on the strictest of those (fusion implies the others).
if(TARGET gate_fusion AND TARGET gate_dash)
    add_subdirectory(rpc)
endif()

# gate-server daemon — composition root + CLI parsing. Built only when the
# full RPC stack is available. Depends on CLI11 (header-only via vcpkg) for
# argument parsing and spdlog (already required by gate_inference).
if(TARGET gate_rpc)
    find_package(CLI11 CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)

    add_executable(gate-server src/main.cpp)
    target_link_libraries(gate-server
        PRIVATE
            gate_rpc
            CLI11::CLI11
            spdlog::spdlog
    )
    target_compile_features(gate-server PRIVATE cxx_std_20)
endif()
