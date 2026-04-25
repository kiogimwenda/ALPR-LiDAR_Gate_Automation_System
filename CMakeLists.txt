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
    add_subdirectory(shared/proto)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Server / dashboard / firmware are added in subsequent Phase 4 milestones.
# add_subdirectory(server)
# add_subdirectory(dashboard/backend)
