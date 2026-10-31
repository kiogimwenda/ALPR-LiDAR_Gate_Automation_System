# server/rpc — gRPC server hosting Dashboard + Admin services.
#
# `gate_rpc` is a static library that exposes the gRPC service implementations
# and a Server lifecycle wrapper. Depends on gate_proto (service stubs),
# gate_auth (admin → store), gate_fusion (dashboard → fusion engine), and
# gate_dash (broadcaster). FieldControllerService lands in 4.3.5.

find_package(spdlog CONFIG REQUIRED)

add_library(gate_rpc STATIC
    src/access_control.cpp
    src/admin_service.cpp
    src/dashboard_service.cpp
    src/field_controller_service.cpp
    src/gate_arbiter.cpp
    src/server.cpp
)

target_include_directories(gate_rpc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(gate_rpc
    PUBLIC
        gate_proto
        gate_auth
        gate_fusion
        gate_dash
    PRIVATE
        spdlog::spdlog
)

target_compile_features(gate_rpc PUBLIC cxx_std_20)
