# tests/state_machine — unit tests for the gate transition logic.
#
# Linked against the host-compiled mirror (gate_state_machine, declared
# in firmware/host/CMakeLists.txt) and Catch2WithMain. No ESP-IDF
# dependency: the same translation unit compiles in CI's vanilla
# vcpkg + Catch2 environment.

add_executable(test_state_machine
    state_machine_test.cpp
)

target_link_libraries(test_state_machine
    PRIVATE
        gate_state_machine
        Catch2::Catch2WithMain
)

target_compile_features(test_state_machine PRIVATE cxx_std_20)

catch_discover_tests(test_state_machine)
