# server/dash — Dashboard event broadcaster.
#
# `gate_dash` is a small static library implementing the multi-subscriber
# fan-out hub the gRPC DashboardService::Subscribe RPC will stream from.
# It depends only on `gate_proto` (for DashboardEvent / DashboardSubscription
# and the inner payload messages). No TensorRT, OpenCV, CUDA, or storage.

add_library(gate_dash STATIC
    src/event_broadcaster.cpp
)

target_include_directories(gate_dash
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(gate_dash
    PUBLIC
        gate_proto
)

# pthread is needed for std::condition_variable on glibc.
find_package(Threads REQUIRED)
target_link_libraries(gate_dash PUBLIC Threads::Threads)

target_compile_features(gate_dash PUBLIC cxx_std_20)
