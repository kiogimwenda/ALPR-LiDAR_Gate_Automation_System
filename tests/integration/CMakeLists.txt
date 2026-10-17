# tests/integration — End-to-end gRPC roundtrip tests.
#
# These spin up real gate::rpc::Server instances on ephemeral ports,
# build real client stubs over those channels, and exercise each
# service end-to-end. Slower than the handler-level tests in tests/rpc
# (each case starts and tears down a gRPC server), but the only way
# to verify the bidi Control stream and the Subscribe streaming RPC
# under realistic conditions.

add_executable(test_integration_grpc_roundtrip grpc_roundtrip_test.cpp)

target_link_libraries(test_integration_grpc_roundtrip
    PRIVATE
        gate_rpc
        Catch2::Catch2WithMain
)

target_compile_features(test_integration_grpc_roundtrip PRIVATE cxx_std_20)

# Each case in this binary runs a full server lifecycle, so give the
# test discovery + execution a generous default timeout.
catch_discover_tests(test_integration_grpc_roundtrip
    PROPERTIES TIMEOUT 30
)

# Per-RPC roles over real mTLS: the test mints a throwaway site PKI with
# scripts/gen-tls-certs.sh + issue-gate-cert.sh (needs bash + openssl).
add_executable(test_integration_mtls_access mtls_access_test.cpp)
target_link_libraries(test_integration_mtls_access
    PRIVATE
        gate_rpc
        Catch2::Catch2WithMain
)
target_compile_definitions(test_integration_mtls_access PRIVATE
    GATE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_features(test_integration_mtls_access PRIVATE cxx_std_20)
catch_discover_tests(test_integration_mtls_access
    PROPERTIES TIMEOUT 60
)
