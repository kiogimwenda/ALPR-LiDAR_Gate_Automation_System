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

option(ENABLE_GPU "Build with CUDA/TensorRT GPU support" ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

if(ENABLE_GPU)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
endif()

# Placeholder — subdirectories added as modules are implemented
# add_subdirectory(shared)
# add_subdirectory(server)
# add_subdirectory(firmware)
# add_subdirectory(dashboard/backend)
