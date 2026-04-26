// ethernet.hpp — W5500 SPI-attached ethernet driver wrapper.
//
// Thin C++ wrapper around the espressif/ethernet_init managed component
// (which handles W5500-specific MAC/PHY setup) plus esp_netif + the IDF
// event loop. The wrapper hides the boilerplate sequence:
//
//   esp_netif_init → esp_event_loop_create_default → ethernet_init_all
//   → esp_netif_attach (eth_glue) → esp_eth_start → DHCP
//
// behind a one-shot `Ethernet::start(config)` and surfaces three events
// to the caller via std::function callbacks:
//
//   on_link    : link physical state changed (cable plugged / unplugged)
//   on_got_ip  : DHCP succeeded; the netif now has an IPv4 address
//   on_lost_ip : DHCP lease lost or interface went down
//
// Process-wide singleton: ESP-IDF's event loop and netif are global, so
// running multiple W5500 instances on one chip would require coordinating
// netif handles and event handlers — out of scope for v1.
//
// Pin map and clock speed are configured via menuconfig (CONFIG_EXAMPLE_*
// in the ethernet_init component's Kconfig). A future sub-milestone may
// expose them at runtime; for now the project's `sdkconfig.defaults`
// pins them at the schematic-friendly values.

#pragma once

#include <array>
#include <cstdint>
#include <esp_err.h>
#include <esp_netif_ip_addr.h>
#include <functional>

namespace gate::drivers {

class Ethernet {
public:
    using LinkCallback = std::function<void(bool up)>;
    using IpCallback = std::function<void(const esp_ip4_addr_t& ip)>;

    struct Config {
        // Locally-administered MAC. If `mac == {0}` (the default), the
        // start() call derives one from the chip's factory MAC by setting
        // the locally-administered bit (0x02) on byte 0.
        std::array<std::uint8_t, 6> mac{};
        // Hostname advertised in the DHCP DISCOVER. Empty falls back to
        // ESP-IDF's compile-time default (CONFIG_LWIP_LOCAL_HOSTNAME).
        const char* hostname = "gate-fw";
    };

    // Initialise esp_netif + the IDF event loop, install the W5500 driver
    // selected by Kconfig, attach netif glue, register the lifecycle event
    // handlers, kick the driver. Returns ESP_OK on success; subsequent
    // calls return ESP_ERR_INVALID_STATE.
    static esp_err_t start(const Config& cfg);

    // Replace (or clear, with `{}`) the link / DHCP callbacks. Safe to call
    // before or after start(). Callbacks fire from the IDF event loop task
    // — not an ISR — so they can take mutexes and call any ESP-IDF API.
    static void on_link(LinkCallback cb);
    static void on_got_ip(IpCallback cb);
    static void on_lost_ip(LinkCallback cb);

    // Snapshot accessors. is_up() goes true on link-up, false on link-down.
    // current_ip() returns {0} until DHCP completes, then the leased IP.
    static bool is_up();
    static esp_ip4_addr_t current_ip();
};

}  // namespace gate::drivers
