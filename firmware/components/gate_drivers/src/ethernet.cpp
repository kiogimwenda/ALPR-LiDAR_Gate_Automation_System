// ethernet.cpp

#include "gate_drivers/ethernet.hpp"

#include <atomic>
#include <cstring>
#include <esp_eth.h>
#include <esp_eth_netif_glue.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <ethernet_init.h>
#include <utility>

namespace gate::drivers {

namespace {

constexpr const char* kTag = "eth-w5500";

// Process-wide state. Held inside an anonymous namespace so the symbols
// have internal linkage; only the public class methods reach them.
struct State {
    std::atomic<bool> started{false};
    std::atomic<bool> link_up{false};
    std::atomic<std::uint32_t> ip_addr{0};  // network byte order; 0 means none
    Ethernet::LinkCallback link_cb;
    Ethernet::IpCallback got_ip_cb;
    Ethernet::LinkCallback lost_ip_cb;
    esp_netif_t* netif = nullptr;
};

State& state() {
    static State s;
    return s;
}

void on_eth_event(void* /*arg*/, esp_event_base_t /*base*/, std::int32_t event_id, void* /*data*/) {
    auto& s = state();
    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(kTag, "link up");
            s.link_up.store(true);
            if (s.link_cb)
                s.link_cb(true);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(kTag, "link down");
            s.link_up.store(false);
            s.ip_addr.store(0);
            if (s.link_cb)
                s.link_cb(false);
            if (s.lost_ip_cb)
                s.lost_ip_cb(false);
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(kTag, "driver started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(kTag, "driver stopped");
            break;
        default:
            break;
    }
}

void on_ip_event(void* /*arg*/, esp_event_base_t /*base*/, std::int32_t event_id, void* data) {
    auto& s = state();
    if (event_id == IP_EVENT_ETH_GOT_IP) {
        const auto* event = static_cast<ip_event_got_ip_t*>(data);
        s.ip_addr.store(event->ip_info.ip.addr);
        ESP_LOGI(kTag, "got IP " IPSTR " gw " IPSTR, IP2STR(&event->ip_info.ip),
                 IP2STR(&event->ip_info.gw));
        if (s.got_ip_cb)
            s.got_ip_cb(event->ip_info.ip);
    } else if (event_id == IP_EVENT_ETH_LOST_IP) {
        ESP_LOGI(kTag, "lost IP");
        s.ip_addr.store(0);
        if (s.lost_ip_cb)
            s.lost_ip_cb(false);
    }
}

esp_err_t derive_mac_if_blank(std::array<std::uint8_t, 6>& mac) {
    bool all_zero = true;
    for (auto b : mac) {
        if (b != 0) {
            all_zero = false;
            break;
        }
    }
    if (!all_zero)
        return ESP_OK;

    // Pull the chip's factory MAC, force the locally-administered bit.
    std::uint8_t base[6];
    esp_err_t err = esp_efuse_mac_get_default(base);
    if (err != ESP_OK)
        return err;
    base[0] = static_cast<std::uint8_t>((base[0] & 0xFE) | 0x02);
    std::memcpy(mac.data(), base, sizeof(base));
    return ESP_OK;
}

}  // namespace

esp_err_t Ethernet::start(const Config& cfg) {
    auto& s = state();
    if (s.started.exchange(true)) {
        return ESP_ERR_INVALID_STATE;  // already running
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // ethernet_init_all reads its Kconfig (CONFIG_ETH_USE_SPI_ETHERNET +
    // CONFIG_ETHERNET_SPI_DEV0_W5500 + CONFIG_EXAMPLE_ETH_SPI_*) to know
    // which chip and which SPI bus + pins to use. Returns an array of
    // handles — for our single-W5500 setup, exactly one element.
    esp_eth_handle_t* handles = nullptr;
    std::uint8_t port_count = 0;
    ESP_ERROR_CHECK(ethernet_init_all(&handles, &port_count));
    if (port_count != 1) {
        ESP_LOGE(kTag, "expected exactly 1 ethernet port, got %u",
                 static_cast<unsigned>(port_count));
        return ESP_ERR_INVALID_STATE;
    }

    // Create the netif and attach the eth-glue. Default config is fine —
    // we only have one ethernet interface and we're using DHCP.
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s.netif = esp_netif_new(&netif_cfg);
    ESP_ERROR_CHECK(esp_netif_attach(s.netif, esp_eth_new_netif_glue(handles[0])));

    if (cfg.hostname != nullptr && cfg.hostname[0] != '\0') {
        ESP_ERROR_CHECK(esp_netif_set_hostname(s.netif, cfg.hostname));
    }

    // Set the MAC. esp_eth_ioctl(ETH_CMD_S_MAC_ADDR) overrides the W5500
    // default. We derive a stable locally-administered MAC from the chip
    // factory ID when the caller didn't supply one explicitly.
    auto mac_copy = cfg.mac;
    ESP_ERROR_CHECK(derive_mac_if_blank(mac_copy));
    ESP_ERROR_CHECK(esp_eth_ioctl(handles[0], ETH_CMD_S_MAC_ADDR, mac_copy.data()));

    // Register lifecycle handlers before starting the driver so we don't
    // race the first link-up event.
    ESP_ERROR_CHECK(
        esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, nullptr));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_ip_event, nullptr));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &on_ip_event, nullptr));

    ESP_ERROR_CHECK(esp_eth_start(handles[0]));
    ESP_LOGI(kTag, "started; awaiting link + DHCP");
    return ESP_OK;
}

void Ethernet::on_link(LinkCallback cb) {
    state().link_cb = std::move(cb);
}
void Ethernet::on_got_ip(IpCallback cb) {
    state().got_ip_cb = std::move(cb);
}
void Ethernet::on_lost_ip(LinkCallback cb) {
    state().lost_ip_cb = std::move(cb);
}

bool Ethernet::is_up() {
    return state().link_up.load();
}

esp_ip4_addr_t Ethernet::current_ip() {
    esp_ip4_addr_t out;
    out.addr = state().ip_addr.load();
    return out;
}

}  // namespace gate::drivers
