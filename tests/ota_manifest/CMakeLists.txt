# tests/ota_manifest — OTA manifest hex decoding + accept/reject rules.

add_executable(test_ota_manifest
    ota_manifest_test.cpp
)

target_link_libraries(test_ota_manifest
    PRIVATE
        gate_ota_manifest
        Catch2::Catch2WithMain
)

catch_discover_tests(test_ota_manifest)
