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

#include "gate_drivers/ethernet.hpp"
#include "gate_drivers/version.hpp"
#include "gate_state_machine/state_machine.hpp"

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

    // Bring up the W5500 ethernet driver. The DHCP-bound IP arrives
    // asynchronously via the on_got_ip callback — we just log it for now;
    // Phase 4.5 will use this signal to start the gRPC Control stream.
    gate::drivers::Ethernet::on_got_ip(
        [](const esp_ip4_addr_t& ip) { ESP_LOGI(kTag, "network ready at " IPSTR, IP2STR(&ip)); });
    gate::drivers::Ethernet::on_link(
        [](bool up) { ESP_LOGI(kTag, "link %s", up ? "up" : "down"); });
    if (gate::drivers::Ethernet::start({}) != ESP_OK) {
        ESP_LOGE(kTag, "ethernet start failed — running offline");
    }

    // Phase 4.5.1 — construct the gate state machine. The driver wiring
    // (limit-switch ISRs → state-machine events → relay/LED actions)
    // lands in 4.5.2, so for now we just log that the skeleton booted
    // and exists. The instance stays in scope across the idle loop so
    // a heap-corruption regression here would be visible at link time
    // rather than later in a hard-to-trace runtime crash.
    using gate::state_machine::StateMachine;
    using gate::state_machine::Config;
    using gate::state_machine::GateType;
    static StateMachine gate_sm{Config{
        .gate_type         = GateType::Sliding,
        .motor_timeout_ms  = 30'000,
        .auto_close_ms     = 0,
    }};
    ESP_LOGI(kTag, "gate state machine ready (state=%.*s, motor_timeout=%ums)",
             static_cast<int>(gate::state_machine::to_string(gate_sm.state()).size()),
             gate::state_machine::to_string(gate_sm.state()).data(),
             static_cast<unsigned>(gate_sm.config().motor_timeout_ms));

    // Phase 4.4 idle loop. Phase 4.5.2 will replace this with the gate
    // state-machine task and the gRPC Control stream client.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
