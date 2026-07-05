cmake_minimum_required(VERSION 3.28)
project(gate-automation
    VERSION 0.0.1
    DESCRIPTION "ALPR + LiDAR Automated Vehicle Gate Control System"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_GPU       "Build with CUDA/TensorRT GPU support"          ON)
option(BUILD_PROTO      "Build the shared gRPC proto library"           ON)
option(BUILD_TESTS      "Build unit and contract tests"                 ON)
option(BUILD_SERVER     "Build the GPU inference + RPC server"          OFF)
option(BUILD_DASHBOARD  "Build the Drogon dashboard backend"            ON)
option(BUILD_SIM        "Build the gate-sim virtual field controller"   ON)

# Compiler warnings — strict on first-party code, relaxed on generated code.
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

if(ENABLE_GPU)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
endif()

if(BUILD_PROTO)
    find_package(Protobuf CONFIG QUIET)
    find_package(gRPC     CONFIG QUIET)
    if(Protobuf_FOUND AND gRPC_FOUND)
        add_subdirectory(shared/proto)
    else()
        message(STATUS
            "Skipping shared/proto: Protobuf and/or gRPC not found via "
            "find_package(CONFIG). Configure with the vcpkg toolchain "
            "(-DCMAKE_TOOLCHAIN_FILE=$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake) "
            "to build the C++ proto stubs. The descriptor-level contract is "
            "still guarded by tests/proto/validate_descriptor.py.")
    endif()
endif()

if(BUILD_SERVER)
    if(NOT ENABLE_GPU)
        message(FATAL_ERROR "BUILD_SERVER=ON requires ENABLE_GPU=ON (CUDA/TensorRT).")
    endif()
    find_package(spdlog CONFIG QUIET)
    if(spdlog_FOUND)
        add_subdirectory(server)
    else()
        message(STATUS
            "Skipping server/: spdlog not found via find_package(CONFIG). "
            "Configure with the vcpkg toolchain to build the inference + RPC server.")
    endif()
endif()

# Dashboard backend (Phase 4.7): Drogon ↔ gRPC bridge. Same vcpkg-only
# gating pattern; Drogon is the heavyweight dependency, so it gets the
# QUIET probe rather than being assumed.
if(BUILD_DASHBOARD AND TARGET gate_proto)
    find_package(Drogon CONFIG QUIET)
    if(Drogon_FOUND)
        add_subdirectory(dashboard/backend)
    else()
        message(STATUS
            "Skipping dashboard/backend: Drogon not found via find_package(CONFIG). "
            "Configure with the vcpkg toolchain to build the dashboard backend.")
    endif()
endif()

# The simulator needs the gRPC stubs (gate_proto), CLI11, and the pure
# firmware mirrors from firmware/host/. It is added before tests/ so
# tests/simulation can gate on the gate_sim_core target. The vcpkg-only
# gating (gate_proto + CLI11) keeps lint-only configures target-free —
# same invariant as the tests block below.
if(BUILD_SIM AND TARGET gate_proto)
    find_package(CLI11 CONFIG QUIET)
    if(CLI11_FOUND)
        add_subdirectory(firmware/host)
        add_subdirectory(simulation)
    else()
        message(STATUS
            "Skipping simulation/: CLI11 not found via find_package(CONFIG). "
            "Configure with the vcpkg toolchain to build gate-sim.")
    endif()
endif()

# Tests come last so any optional production targets (gate_proto, gate_inference)
# are already declared when tests/CMakeLists.txt evaluates `if(TARGET …)` gates.
#
# Pure firmware modules (e.g. the gate state machine) are mirrored as host
# static libraries under firmware/host/ so the Catch2 suite can link them.
# The host mirrors and the tests/ tree are only added when vcpkg-resolved
# dependencies exist — without them there are no consumers, and adding the
# targets unconditionally would surface them in compile_commands.json
# during lint-only configures that intentionally skip vcpkg. The mirror
# block guards against a double add when the simulation block above
# already pulled firmware/host in.
if(BUILD_TESTS)
    find_package(Catch2 3 CONFIG QUIET)
    if(Catch2_FOUND)
        enable_testing()
        if(NOT TARGET gate_state_machine)
            add_subdirectory(firmware/host)
        endif()
        add_subdirectory(tests)
    else()
        message(STATUS
            "Skipping tests/: Catch2 v3 not found via find_package(CONFIG). "
            "Configure with the vcpkg toolchain to build the C++ test suite.")
    endif()
endif()

# Dashboard / firmware are added in subsequent Phase 4 milestones.
# add_subdirectory(dashboard/backend)
