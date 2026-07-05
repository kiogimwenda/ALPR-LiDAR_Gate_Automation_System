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
        app_update        # esp_ota_ops — rollback mark-valid handshake
        esp_system        # esp_get_idf_version, esp_chip_info, esp_restart
        esp_timer         # reboot one-shot
        log               # ESP_LOG*
        freertos          # vTaskDelay, xTaskCreate
        lwip              # esp_netif_sntp (wall clock for telemetry)
        # First-party components:
        gate_control
        gate_drivers
        gate_ota
        gate_rpc
        gate_state_machine  # main.cpp names GateType directly
)
