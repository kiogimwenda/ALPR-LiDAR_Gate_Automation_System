# tests/proto/CMakeLists.txt — contract tests for gate_service.proto.
#
# Goal: every release pipeline asserts that the wire format is stable and
# semantically correct before any firmware or server image is shipped.

add_executable(test_proto_contract
    proto_contract_test.cpp
)

target_link_libraries(test_proto_contract
    PRIVATE
        gate_proto
        Catch2::Catch2WithMain
)

target_compile_features(test_proto_contract PRIVATE cxx_std_20)

catch_discover_tests(test_proto_contract)
