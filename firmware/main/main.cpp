// main.cpp — gate-firmware entry point.
//
// app_main() is the ESP-IDF startup hook. We log a banner with the IDF
// version and the configured chip features so a first-boot serial console
// confirms the build is the one running. The infinite vTaskDelay loop
// keeps the main task alive — actual application logic lands in Phase 4.5.

#include <esp_chip_info.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_netif_ip_addr.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include "gate_control/gate_controller.hpp"
#include "gate_drivers/ethernet.hpp"
#include "gate_drivers/version.hpp"
#include "gate_rpc/control_client.hpp"

namespace {

constexpr const char* kTag = "gate-fw";

void log_boot_banner() {
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    ESP_LOGI(kTag, "gate-firmware %s starting (idf v%d.%d.%d)", gate::drivers::kVersion,
             ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    // `revision` is encoded as MXX — wafer major in the hundreds digit,
    // wafer minor in the lower two. The split here matches the format
    // espefuse / esptool report.
    ESP_LOGI(kTag, "chip: %s rev %u.%u, %u cores, %s%s%s%s%s", CONFIG_IDF_TARGET,
             static_cast<unsigned>(chip.revision / 100), static_cast<unsigned>(chip.revision % 100),
             static_cast<unsigned>(chip.cores),
             (chip.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
             (chip.features & CHIP_FEATURE_BT) ? "BT " : "",
             (chip.features & CHIP_FEATURE_BLE) ? "BLE " : "",
             (chip.features & CHIP_FEATURE_IEEE802154) ? "802.15.4 " : "",
             (chip.features & CHIP_FEATURE_EMB_FLASH) ? "embedded-flash " : "");
}

}  // namespace

extern "C" void app_main(void) {
    log_boot_banner();

    // Phase 4.5.2 — construct and start the gate controller. The
    // constructor configures every gate GPIO (relays off, inputs
    // pulled up) and start() resolves the boot position from the limit
    // switches before spawning the event-pump task, so by the time
    // start() returns the gate is either in a verified position or
    // Faulted awaiting an operator reset. Static storage: the drivers'
    // esp_timers capture `this`, so the controller must live for the
    // whole firmware lifetime.
    using gate::control::GateController;
    static GateController gate_ctrl{GateController::Config{
        .sm =
            {
                .gate_type = gate::state_machine::GateType::Sliding,
                .motor_timeout_ms = 30'000,
                .auto_close_ms = 0,  // auto-close policy arrives with remote config (4.5.4)
            },
        .pins = {},               // DevKitC-1 reference wiring (see gate_controller.hpp)
        .reverse_on_beam = true,  // UL 325-style entrapment protection
        .heartbeat_period_ms = 5'000,
    }};
    ESP_ERROR_CHECK(gate_ctrl.start());

    // Phase 4.5.3 — gRPC Control-stream client (nanopb + nghttp2 h2c,
    // ADR-011). The telemetry filler runs on the gate_rpc task and
    // reads only the controller's lock-free snapshots. Constructed
    // before ethernet start so no got-ip edge can slip past the
    // notify wiring below.
    using gate::rpc::ControlClient;
    static ControlClient control_client{
        ControlClient::Config{
            .host = CONFIG_GATE_SERVER_HOST,
            .port = CONFIG_GATE_SERVER_PORT,
            .gate_id = CONFIG_GATE_ID,
            .fw_version = gate::drivers::kVersion,
        },
        [](gate_v1_Telemetry& t) {
            t.gate_state = gate::rpc::to_wire_state(gate_ctrl.state());
            // Limit/beam booleans join in 4.5.4 when GateController
            // grows input snapshots; state is the load-bearing field.
        },
    };

    // Bring up the W5500 ethernet driver. DHCP completion gates the
    // Control stream: the client connects on got-ip and backs off
    // while the link is down.
    gate::drivers::Ethernet::on_got_ip([](const esp_ip4_addr_t& ip) {
        ESP_LOGI(kTag, "network ready at " IPSTR, IP2STR(&ip));
        control_client.notify_network_up();
    });
    gate::drivers::Ethernet::on_link([](bool up) {
        ESP_LOGI(kTag, "link %s", up ? "up" : "down");
        if (!up) {
            control_client.notify_network_down();
        }
    });
    if (gate::drivers::Ethernet::start({}) != ESP_OK) {
        ESP_LOGE(kTag, "ethernet start failed — running offline");
    }
    ESP_ERROR_CHECK(control_client.start());

    // Idle loop — gate work happens on gate_ctrl, RPC on gate_rpc.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
