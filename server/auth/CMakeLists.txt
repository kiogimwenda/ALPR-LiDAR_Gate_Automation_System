# server/auth — Allowlist + blocklist store and authorization helpers.
#
# `gate_auth` is a small static library wrapping a sqlite3 database. It does
# not depend on TensorRT, OpenCV, or CUDA — only on `gate_proto` (for the
# Allowlistentry / TimeWindow / VehicleClass types) and the system sqlite3.
#
# The library is consumed by the fusion engine (Phase 4.3.2) and the gRPC
# AdminService (Phase 4.3.4).

find_package(SQLite3 REQUIRED)
message(STATUS "Found SQLite3 ${SQLite3_VERSION} (${SQLite3_LIBRARY})")

add_library(gate_auth STATIC
    src/allowlist_store.cpp
)

target_include_directories(gate_auth
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(gate_auth
    PUBLIC
        gate_proto
    PRIVATE
        SQLite::SQLite3
)

target_compile_features(gate_auth PUBLIC cxx_std_20)

# Generated proto headers trip the project's strict warnings; mark the proto
# include path SYSTEM via the gate_proto target's INTERFACE settings, which
# already does this. Nothing extra needed here.
