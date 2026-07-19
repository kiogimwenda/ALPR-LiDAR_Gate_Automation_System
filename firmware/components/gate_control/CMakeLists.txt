# components/gate_control — driver layer for the gate state machine.
#
# Owns the GateController: the FreeRTOS event-pump task, the hardware
# driver instances, and the two timers (motor watchdog, auto-close) the
# pure state machine deliberately does not know about. This component is
# ESP-IDF-only by design — the hardware-agnostic logic it animates lives
# in gate_state_machine, which is what the host test suite exercises.

idf_component_register(
    SRCS
        "src/gate_controller.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        # Public: the header exposes gpio_num_t, esp_timer/FreeRTOS
        # handles, and the driver / state-machine types.
        esp_driver_gpio
        esp_timer
        freertos
        gate_drivers
        gate_state_machine
    PRIV_REQUIRES
        log
)
