# tests/firmware_integration — end-to-end scenarios over the pure
# firmware modules (state machine + command tracker + framing),
# composed the way gate_control/gate_rpc wire them on the ESP32-S3.

add_executable(test_firmware_integration
    gate_scenarios_test.cpp
)

target_link_libraries(test_firmware_integration
    PRIVATE
        gate_rpc_framing   # framing codec + command tracker (links gate_state_machine)
        Catch2::Catch2WithMain
)

catch_discover_tests(test_firmware_integration)
