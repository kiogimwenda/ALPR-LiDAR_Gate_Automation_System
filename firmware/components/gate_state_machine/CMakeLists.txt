# components/gate_state_machine — pure-C++20 gate transition logic.
#
# Carries no ESP-IDF dependencies on purpose. The same translation unit
# is compiled for host (see firmware/host/CMakeLists.txt — pulled in by
# the top-level build) so Catch2 tests under tests/state_machine/ run
# against the exact code that runs on the ESP32-S3. Phase 4.5.2 wires
# this component into gate_drivers; until then it links cleanly as a
# leaf with no REQUIRES on driver state.

idf_component_register(
    SRCS
        "src/state_machine.cpp"
    INCLUDE_DIRS
        "include"
)
