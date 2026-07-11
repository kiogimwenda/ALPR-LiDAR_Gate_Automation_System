# dashboard/backend — Drogon ↔ gRPC bridge (ADR-003, Phase 4.7).
#
# Split so the JSON presentation layer stays Drogon-free and
# unit-testable:
#
#   gate_dash_json  — protobuf → JSON mapping (gate_proto + jsoncpp)
#   gate_dash_auth  — PBKDF2 credentials + JWT sessions (4.10.2;
#                     jwt-cpp + OpenSSL, no Drogon — unit-testable)
#   gate_dash_api   — GrpcBridge + controllers (adds Drogon)
#   gate-dashboard  — the executable

find_package(jsoncpp CONFIG REQUIRED)
find_package(CLI11  CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)  # jwt-cpp JSON traits
find_package(OpenSSL REQUIRED)

add_library(gate_dash_json STATIC
    src/json_mapping.cpp
    src/event_cache.cpp
)

target_include_directories(gate_dash_json PUBLIC include)

target_link_libraries(gate_dash_json
    PUBLIC
        gate_proto
        JsonCpp::JsonCpp
)

target_compile_features(gate_dash_json PUBLIC cxx_std_20)

add_library(gate_dash_auth STATIC
    src/auth.cpp
)

target_include_directories(gate_dash_auth PUBLIC include)

target_link_libraries(gate_dash_auth
    PRIVATE
        jwt-cpp::jwt-cpp
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
)

target_compile_features(gate_dash_auth PUBLIC cxx_std_20)

add_library(gate_dash_api STATIC
    src/grpc_bridge.cpp
    src/event_stream.cpp
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
    src/controllers/gates_controller.cpp
    src/controllers/allowlist_controller.cpp
    src/controllers/status_controller.cpp
    src/controllers/events_ws_controller.cpp
    src/controllers/auth_controller.cpp
    src/controllers/auth_filter.cpp
)

target_link_libraries(gate-dashboard
    PRIVATE
        gate_dash_api
        gate_dash_auth
        CLI11::CLI11
)
