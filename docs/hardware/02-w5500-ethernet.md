# Component Guide 02: W5500 Ethernet Module — Wired Network for ESP32-S3

## What This Part Does

This small board gives the ESP32-S3 a wired Ethernet connection (the same kind of cable you plug into a router). The ESP32 uses this to talk to the GPU server over the local network. WiFi is too unreliable for real-time gate control — wired Ethernet is fast and dependable.

## Exact Part

- **Chip:** WIZnet W5500
- **Module:** W5500 Ethernet Network Module (the small green/blue PCB with an RJ45 jack)
- **Interface:** SPI (4 wires + power)
- **Speed:** 10/100 Mbps
- **Voltage:** 3.3V or 5V (has onboard regulator)

**Do NOT buy:** the W5100 or ENC28J60 modules — they are older and slower. Make sure the listing says **W5500**.

## Where to Buy

| Source | Approximate Price | Notes |
|--------|-------------------|-------|
| **AliExpress** — search "W5500 Ethernet module SPI" | USD 2–4 (~KES 260–520) | Ships in 15–30 days. Very common part. |
| **Luthuli Avenue, Nairobi** — Nuvision, Kemu, Redflag Electronics | KES 350–600 | Ask for "W5500 Ethernet module, the one with the RJ45 plug." |
| **Amazon** — search "W5500 Ethernet module" | USD 4–7 | HiLetgo and DIYables brands are reliable. |

**Lead time:** AliExpress 2–4 weeks. Nairobi: same day if in stock.

## What's in the Box

- 1× W5500 module board (small PCB with an RJ45 jack on one end and pin headers on the other)
- Usually comes with pin headers pre-soldered
- You need to buy separately:
  - 1× Ethernet cable (Cat5e or Cat6, any length — 1m for bench testing)
  - 6× jumper wires (female-to-male if using breadboard, or female-to-female for direct connection)

## What's NOT in the Box

- No Ethernet cable — buy one
- No documentation — this guide is your documentation

## Pinout and Wiring

```
  W5500 Module              ESP32-S3-DevKitC-1
  ┌───────────┐
  │  RJ45     │
  │  Jack     │
  │           │
  │  VCC  ────┼──────────── 3.3V (pin 1 or 2)
  │  GND  ────┼──────────── GND  (pin 21 or 22)
  │  MISO ────┼──────────── IO12 (SPI MISO)
  │  MOSI ────┼──────────── IO11 (SPI MOSI)
  │  SCLK ────┼──────────── IO10 (SPI CLK)
  │  CS   ────┼──────────── IO13 (SPI CS)
  │  INT  ────┼──────────── IO14 (Interrupt — optional but recommended)
  │  RST  ────┼──────────── 3.3V (tie high — or connect to a GPIO if you want software reset)
  └───────────┘

  IMPORTANT: Connect VCC to 3.3V, NOT 5V.
  Some modules have a 5V-tolerant regulator, but 3.3V is safest
  and matches the ESP32-S3's logic level.
```

### Wiring Table

| W5500 Pin | Wire Color (suggested) | ESP32-S3 Pin | Notes |
|-----------|----------------------|--------------|-------|
| VCC | Red | 3V3 | Power — 3.3V only |
| GND | Black | GND | Ground — must be shared |
| MISO | Yellow | IO12 | Data from W5500 to ESP32 |
| MOSI | Green | IO11 | Data from ESP32 to W5500 |
| SCLK | Blue | IO10 | Clock signal |
| CS | Orange | IO13 | Chip select — tells W5500 "I'm talking to you" |
| INT | White | IO14 | Interrupt — W5500 signals "I have data for you" |
| RST | Red (or jumper) | 3V3 | Keep high (active-low reset) |

## Step-by-Step Assembly

### What You Need

- [ ] W5500 Ethernet module
- [ ] ESP32-S3-DevKitC-1 (already on breadboard from Guide 01)
- [ ] 8× jumper wires (male-to-male for breadboard)
- [ ] 1× Ethernet cable
- [ ] Network switch or router with a free Ethernet port

### Steps

**Step 1 — Place the W5500 on the breadboard.**
Place the W5500 module on the same breadboard as the ESP32-S3, leaving at least 5 rows of space between them. The RJ45 jack should face away from the ESP32 for cable clearance.

**Step 2 — Connect power.**
- Red wire from W5500 **VCC** to the ESP32-S3 **3V3** pin.
- Black wire from W5500 **GND** to the ESP32-S3 **GND** pin.

**Step 3 — Connect SPI data lines.**
- Yellow wire: W5500 **MISO** → ESP32-S3 **IO12**
- Green wire: W5500 **MOSI** → ESP32-S3 **IO11**
- Blue wire: W5500 **SCLK** → ESP32-S3 **IO10**
- Orange wire: W5500 **CS** → ESP32-S3 **IO13**

**Step 4 — Connect interrupt (optional but recommended).**
- White wire: W5500 **INT** → ESP32-S3 **IO14**

**Step 5 — Connect reset.**
- Short jumper wire or another red wire: W5500 **RST** → ESP32-S3 **3V3** (same power rail).

**Step 6 — Double-check every wire.**
Go through the wiring table above and confirm each wire is in the right place. The most common mistake is swapping MISO and MOSI — MISO is data OUT of the W5500, MOSI is data IN.

**Step 7 — Plug in the Ethernet cable.**
Connect an Ethernet cable from the W5500's RJ45 jack to your network switch or router.

## First Power-On

**What you should see:**
1. When you plug in the ESP32 USB cable, the W5500's green/yellow LEDs on the RJ45 jack should light up within 2 seconds (if an Ethernet cable is connected to a switch).
2. Green LED = link is up (connected). Yellow/amber LED = activity (data transfer).

**If the LEDs do NOT light up:**
- Check the Ethernet cable — try a different one.
- Check that VCC is connected to 3.3V (not floating).
- Check that GND is connected.
- Make sure the other end of the Ethernet cable is plugged into a powered switch/router.

## Bring-Up Test Program

This program initializes the W5500, gets an IP address via DHCP, and pings the gateway. Run this on the ESP32-S3 using ESP-IDF.

```cpp
// main/main.cpp — W5500 Ethernet bring-up test (ESP-IDF)
#include <cstdio>
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "eth_test";

static void eth_event_handler(void* arg, esp_event_base_t base,
                              int32_t event_id, void* data) {
    if (event_id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Ethernet link UP");
    } else if (event_id == ETHERNET_EVENT_DISCONNECTED) {
        ESP_LOGI(TAG, "Ethernet link DOWN");
    }
}

static void ip_event_handler(void* arg, esp_event_base_t base,
                             int32_t event_id, void* data) {
    if (event_id == IP_EVENT_ETH_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "W5500 Ethernet test PASSED!");
    }
}

extern "C" void app_main() {
    printf("W5500 Ethernet Bring-Up Test\n");

    esp_netif_init();
    esp_event_loop_create_default();

    // SPI bus config
    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_NUM_11,
        .miso_io_num = GPIO_NUM_12,
        .sclk_io_num = GPIO_NUM_10,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);

    // W5500 SPI device config
    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = 20'000'000,  // 20 MHz
        .spics_io_num = GPIO_NUM_13,
        .queue_size = 20,
    };

    // Ethernet MAC + PHY config for W5500
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI3_HOST, &devcfg);
    w5500_config.int_gpio_num = GPIO_NUM_14;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = nullptr;
    esp_eth_driver_install(&eth_config, &eth_handle);

    // Create netif and attach
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t* eth_netif = esp_netif_new(&netif_cfg);
    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle));

    // Register event handlers
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip_event_handler, nullptr);

    // Start Ethernet
    esp_eth_start(eth_handle);
    ESP_LOGI(TAG, "Waiting for Ethernet link and DHCP...");
}
```

**Expected output:**
```
W5500 Ethernet Bring-Up Test
I (xxx) eth_test: Waiting for Ethernet link and DHCP...
I (xxx) eth_test: Ethernet link UP
I (xxx) eth_test: Got IP: 192.168.1.xxx
I (xxx) eth_test: Gateway: 192.168.1.1
I (xxx) eth_test: W5500 Ethernet test PASSED!
```

## Failure Signatures

| Symptom | Cause | Fix |
|---------|-------|-----|
| RJ45 LEDs stay off | No power to W5500, or no Ethernet cable | Check VCC→3V3 and GND wires. Check cable. |
| RJ45 green LED on but no IP address | SPI wiring wrong | Swap MISO and MOSI. Check SCLK and CS pins. |
| "SPI device not found" error | Wrong SPI host or pins | Verify pin numbers match IO10/11/12/13. |
| IP address obtained but can't reach server | Wrong network/subnet | Ensure W5500 and GPU server are on the same switch/VLAN. |
| Intermittent disconnections | Bad jumper wire contact | Use shorter wires. Press connections firmly into breadboard. For production: solder directly. |

## Datasheet Links

- [W5500 Chip Datasheet (WIZnet official)](https://docs.wiznet.io/Product/iEthernet/W5500/datasheet)
- [ESP-IDF W5500 Ethernet Driver Docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/network/esp_eth.html)

## Safety Notes

- **No dangerous voltages** — this board runs at 3.3V.
- **Do not connect to a PoE switch port** unless you have verified the module has PoE protection. Standard W5500 modules do NOT support PoE and can be damaged by PoE voltage (48V). Use a non-PoE port or a PoE splitter.
- **Ethernet cables carry low voltage** — safe to handle.
