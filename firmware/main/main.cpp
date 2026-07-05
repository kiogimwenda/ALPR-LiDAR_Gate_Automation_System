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
#include <esp_netif_sntp.h>
#include <esp_system.h>
#include <esp_timer.h>
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

    // REBOOT commands ack over the stream first, then restart 1.5 s
    // later via a one-shot — a blocking delay in the dispatcher would
    // stall the HTTP/2 pump and the ack would never flush.
    static esp_timer_handle_t reboot_timer = nullptr;
    {
        esp_timer_create_args_t targs = {};
        targs.callback = [](void*) { esp_restart(); };
        targs.dispatch_method = ESP_TIMER_TASK;
        targs.name = "gate_reboot";
        ESP_ERROR_CHECK(esp_timer_create(&targs, &reboot_timer));
    }

    // Phase 4.5.3/4.5.4 — gRPC Control-stream client (nanopb + nghttp2
    // h2c, ADR-011). Telemetry filler and command dispatcher both run
    // on the gate_rpc task and only touch GateController's thread-safe
    // surfaces (lock-free snapshots, queue-posting command entry
    // points). Constructed before ethernet start so no got-ip edge can
    // slip past the notify wiring below.
    using gate::rpc::ControlClient;
    using gate::state_machine::State;
    static ControlClient control_client{
        ControlClient::Config{
            .host = CONFIG_GATE_SERVER_HOST,
            .port = CONFIG_GATE_SERVER_PORT,
            .gate_id = CONFIG_GATE_ID,
            .fw_version = gate::drivers::kVersion,
        },
        [](gate_v1_Telemetry& t) {
            t.gate_state = gate::rpc::to_wire_state(gate_ctrl.state());
            t.limit_open = gate_ctrl.limit_open_active();
            t.limit_closed = gate_ctrl.limit_closed_active();
            t.safety_beam_clear = !gate_ctrl.beam_blocked();
        },
        [](const gate_v1_GateCommand& cmd) -> ControlClient::DispatchResult {
            switch (cmd.kind) {
                case gate_v1_CommandKind_COMMAND_KIND_OPEN_GATE:
                    gate_ctrl.command_open();
                    return {.accepted = true, .terminal_ok = State::Open};
                case gate_v1_CommandKind_COMMAND_KIND_CLOSE_GATE:
                    gate_ctrl.command_close();
                    return {.accepted = true, .terminal_ok = State::Closed};
                case gate_v1_CommandKind_COMMAND_KIND_LED_PATTERN:
                    if (cmd.led_pattern > _gate_v1_LedPattern_MAX) {
                        return {.error = "unknown led pattern"};
                    }
                    // Wire values 0–5 mirror StatusLed::Pattern by design.
                    gate_ctrl.show_led_pattern(
                        static_cast<gate::drivers::StatusLed::Pattern>(cmd.led_pattern));
                    return {.accepted = true, .completed_now = true};
                case gate_v1_CommandKind_COMMAND_KIND_REBOOT:
                    ESP_LOGW(kTag, "reboot commanded by %s", cmd.actor);
                    ESP_ERROR_CHECK(esp_timer_start_once(reboot_timer, 1'500'000));
                    return {.accepted = true, .completed_now = true};
                case gate_v1_CommandKind_COMMAND_KIND_BEGIN_OTA:
                    return {.error = "OTA delivery lands in Phase 4.5.5"};
                case gate_v1_CommandKind_COMMAND_KIND_PULSE_RELAY:
                case gate_v1_CommandKind_COMMAND_KIND_LATCH_OPEN:
                case gate_v1_CommandKind_COMMAND_KIND_LATCH_CLOSE:
                case gate_v1_CommandKind_COMMAND_KIND_RELEASE_LATCH:
                    // The residential profile drives two motion
                    // contactors directly — there is no third-party
                    // opener behind a trigger relay to pulse or latch.
                    return {.error = "unsupported on the residential controller profile"};
                default:
                    return {.error = "unspecified command kind"};
            }
        },
    };

    // Completion acks for async commands: gate transitions (and
    // reasoned rejections) flow from the gate_ctrl pump into the
    // client's command tracker. Must be wired before gate_ctrl.start().
    gate_ctrl.set_transition_listener(
        [](State s, gate::state_machine::Reason r, bool transitioned) {
            control_client.notify_gate_event(s, r, transitioned);
        });
    ESP_ERROR_CHECK(gate_ctrl.start());

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

    // Wall clock for Telemetry/CommandAck timestamps. Init is safe
    // pre-DHCP (it retries internally); until the first sync the RPC
    // layer omits timestamps instead of sending epoch garbage.
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_GATE_SNTP_SERVER);
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_cfg));

    ESP_ERROR_CHECK(control_client.start());

    // Idle loop — gate work happens on gate_ctrl, RPC on gate_rpc.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
