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
option(BUILD_DASHBOARD  "Build the Drogon dashboard backend"            OFF)

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

if(BUILD_TESTS)
    find_package(Catch2 3 CONFIG QUIET)
    if(Catch2_FOUND)
        enable_testing()
        add_subdirectory(tests)
    else()
        message(STATUS
            "Skipping tests/: Catch2 v3 not found via find_package(CONFIG). "
            "Configure with the vcpkg toolchain to build the C++ test suite.")
    endif()
endif()

# Server / dashboard / firmware are added in subsequent Phase 4 milestones.
# add_subdirectory(server)
# add_subdirectory(dashboard/backend)
