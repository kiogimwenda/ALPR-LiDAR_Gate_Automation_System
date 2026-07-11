# main/ — application entry component.
#
# Holds app_main() and the wire-up between FreeRTOS, the gate_drivers
# component (Phase 4.4.2+), and the application logic (Phase 4.5).

# Site PKI (Phase 4.10.3) — embedded only when TLS is enabled, so a
# fresh checkout builds without certificates. When enabled, a missing
# PEM fails the build here rather than the gate silently shipping
# without its identity (see certs/README.md).
set(GATE_TLS_EMBED "")
if(CONFIG_GATE_TLS_ENABLE)
    list(APPEND GATE_TLS_EMBED
        "certs/site_ca.pem"
        "certs/gate.pem"
        "certs/gate.key"
    )
endif()

idf_component_register(
    SRCS
        "main.cpp"
    INCLUDE_DIRS
        "."
    EMBED_TXTFILES
        ${GATE_TLS_EMBED}
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
