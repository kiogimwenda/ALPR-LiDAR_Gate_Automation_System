# main/ — application entry component.
#
# Holds app_main() and the wire-up between FreeRTOS, the gate_drivers
# component (Phase 4.4.2+), and the application logic (Phase 4.5).

idf_component_register(
    SRCS
        "main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        # System components used by main:
        esp_system        # esp_get_idf_version, esp_chip_info
        log               # ESP_LOG*
        freertos          # vTaskDelay, xTaskCreate
        # First-party components:
        gate_control
        gate_drivers
        gate_state_machine  # main.cpp names GateType directly
)
