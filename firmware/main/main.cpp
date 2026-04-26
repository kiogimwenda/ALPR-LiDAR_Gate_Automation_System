// main.cpp — gate-firmware entry point.
//
// app_main() is the ESP-IDF startup hook. We log a banner with the IDF
// version and the configured chip features so a first-boot serial console
// confirms the build is the one running. The infinite vTaskDelay loop
// keeps the main task alive — actual application logic lands in Phase 4.5.

#include <esp_chip_info.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "gate_drivers/version.hpp"

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

    // Phase 4.4.1 idle loop. Phase 4.5 will replace this with the
    // gate state-machine task and the gRPC Control stream client.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
