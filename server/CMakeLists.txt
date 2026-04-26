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
