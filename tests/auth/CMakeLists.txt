# tests/auth — unit tests for the allowlist/blocklist store.
#
# Linked against gate_auth (which transitively pulls in gate_proto) and
# Catch2WithMain. No GPU dependency: this test target builds and runs in
# CI's CPU-only configuration as long as BUILD_PROTO=ON.

add_executable(test_auth_allowlist_store
    allowlist_store_test.cpp
)

target_link_libraries(test_auth_allowlist_store
    PRIVATE
        gate_auth
        Catch2::Catch2WithMain
)

target_compile_features(test_auth_allowlist_store PRIVATE cxx_std_20)

# Generated proto headers (transitively included via gate_auth → gate_proto)
# trigger -Wpedantic and -Wdeprecated-declarations on Catch2's expectations
# of the proto-public reflection API. Relax both for this test only.
target_compile_options(test_auth_allowlist_store PRIVATE
    -Wno-deprecated-declarations
)

catch_discover_tests(test_auth_allowlist_store)
