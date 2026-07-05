# dashboard/backend — Drogon ↔ gRPC bridge (ADR-003, Phase 4.7).
#
# Split so the JSON presentation layer stays Drogon-free and
# unit-testable:
#
#   gate_dash_json  — protobuf → JSON mapping (gate_proto + jsoncpp)
#   gate_dash_api   — GrpcBridge + controllers (adds Drogon)
#   gate-dashboard  — the executable

find_package(jsoncpp CONFIG REQUIRED)
find_package(CLI11  CONFIG REQUIRED)

add_library(gate_dash_json STATIC
    src/json_mapping.cpp
)

target_include_directories(gate_dash_json PUBLIC include)

target_link_libraries(gate_dash_json
    PUBLIC
        gate_proto
        JsonCpp::JsonCpp
)

target_compile_features(gate_dash_json PUBLIC cxx_std_20)

add_library(gate_dash_api STATIC
    src/grpc_bridge.cpp
)

target_link_libraries(gate_dash_api
    PUBLIC
        gate_dash_json
        Drogon::Drogon
)

# Controllers compile into the executable, not the static library:
# Drogon registers them via global constructors, and the linker drops
# unreferenced objects from archives — a controller in a .a silently
# 404s.
add_executable(gate-dashboard
    src/main.cpp
    src/controllers/health_controller.cpp
)

target_link_libraries(gate-dashboard
    PRIVATE
        gate_dash_api
        CLI11::CLI11
)
