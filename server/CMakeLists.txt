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
