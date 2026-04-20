# Component Guide 01: ESP32-S3-DevKitC-1 (N16R8) — Field PCB Brain

## What This Part Does

This is the main computer that sits at the gate. It reads the camera and LiDAR, talks to the GPU server over Ethernet, controls the gate motor relays, and logs events locally. Think of it as the "brain" of the gate controller.

## Exact Part

- **Manufacturer:** Espressif Systems
- **Model:** ESP32-S3-DevKitC-1-N16R8
- **Module on board:** ESP32-S3-WROOM-1-N16R8
- **Key specs:** Dual-core Xtensa LX7 @ 240 MHz, 16 MB Flash, 8 MB PSRAM, WiFi + BLE 5.0, USB OTG
- **Variant:** You MUST get the **N16R8** variant (16 MB flash, 8 MB PSRAM). The N8R2 or N4R2 variants have less memory and will not work.

## Where to Buy

| Source | Approximate Price | Notes |
|--------|-------------------|-------|
| **AliExpress** — search "ESP32-S3-DevKitC-1 N16R8" | USD 8–12 (~KES 1,050–1,550) | Ships in 15–30 days. Buy from sellers with 95%+ rating. |
| **Luthuli Avenue, Nairobi** — Nuvision Electronics, Kemu Electronics | KES 1,200–1,800 | Ask specifically for "ESP32-S3 with 8 megabytes PSRAM, the N16R8 version." |
| **Amazon** — search "ESP32-S3-DevKitC-1 N16R8" | USD 10–15 | Faster shipping if ordering from US/EU. |

**Lead time:** AliExpress 2–4 weeks. Nairobi shops: same day if in stock.

## What's in the Box

- 1× ESP32-S3-DevKitC-1 board (the green PCB with the metal-shielded module on it)
- 2× 20-pin male header strips (may come pre-soldered or loose)
- That's it. You need to buy separately:
  - 1× USB-C cable (for programming and power during development)
  - 1× breadboard (for prototyping — 830-point full-size)
  - Jumper wires (male-to-male, at least 20 pieces)

## Pinout

```
              ┌──────────────────┐
              │    USB-C Port    │
              │   ┌──────────┐   │
              │   │ ESP32-S3 │   │
              │   │ WROOM-1  │   │
              │   │  N16R8   │   │
              │   │ (metal   │   │
              │   │  shield) │   │
              │   └──────────┘   │
              │                  │
  3V3 ────────┤ 1            44 ├──────── 3V3
  3V3 ────────┤ 2            43 ├──────── RST (Reset button)
  IO4 ────────┤ 3            42 ├──────── IO48 (RGB LED)
  IO5 ────────┤ 4            41 ├──────── IO47
  IO6 ────────┤ 5            40 ├──────── IO21
  IO7 ────────┤ 6            39 ├──────── IO20 (USB D+)
 IO15 ────────┤ 7            38 ├──────── IO19 (USB D-)
 IO16 ────────┤ 8            37 ├──────── IO42
 IO17 ────────┤ 9            36 ├──────── IO41
 IO18 ────────┤10            35 ├──────── IO40
  IO8 ────────┤11            34 ├──────── IO39
  IO3 ────────┤12            33 ├──────── IO38
 IO46 ────────┤13            32 ├──────── (NC)
  IO9 ────────┤14            31 ├──────── (NC)
 IO10 ────────┤15            30 ├──────── IO13 ← W5500 SPI CS
 IO11 ────────┤16            29 ├──────── IO12 ← W5500 SPI MISO
 IO12 ────────┤17            28 ├──────── IO11 ← W5500 SPI MOSI
 IO13 ────────┤18            27 ├──────── IO10 ← W5500 SPI CLK
 IO14 ────────┤19            26 ├──────── IO9
  5V  ────────┤20            25 ├──────── IO46
  GND ────────┤21            24 ├──────── IO3
  GND ────────┤22            23 ├──────── IO8
              └──────────────────┘

  IMPORTANT: Pins IO35, IO36, IO37 are used internally for
  PSRAM/Flash on the N16R8 variant. DO NOT connect anything
  to these pins — they are not broken out on this board.
```

### Pin Assignments for This Project

| Pin | Function | Connects To |
|-----|----------|-------------|
| IO10 | SPI CLK | W5500 Ethernet SCLK |
| IO11 | SPI MOSI | W5500 Ethernet MOSI |
| IO12 | SPI MISO | W5500 Ethernet MISO |
| IO13 | SPI CS | W5500 Ethernet CS |
| IO14 | SPI INT | W5500 Ethernet INT |
| IO4 | GPIO OUT | Relay 1 (gate leaf 1 trigger) |
| IO5 | GPIO OUT | Relay 2 (gate leaf 2 / spare) |
| IO6 | GPIO IN (pull-up) | Limit switch — gate OPEN |
| IO7 | GPIO IN (pull-up) | Limit switch — gate CLOSED |
| IO15 | GPIO IN (pull-up) | Safety beam sensor |
| IO16 | UART TX | RS-485 TX (future — CENTURION motor) |
| IO17 | UART RX | RS-485 RX (future — CENTURION motor) |
| IO47 | GPIO OUT | Status LED — green (authorized) |
| IO48 | RGB LED | Built-in addressable LED (system status) |
| 5V | Power | From power supply |
| GND | Ground | Common ground |

## Step-by-Step Assembly

### What You Need Before Starting

- [ ] ESP32-S3-DevKitC-1-N16R8 board
- [ ] USB-C cable
- [ ] Breadboard (830-point)
- [ ] Computer with USB port

### Steps

**Step 1 — Inspect the board.**
Pick up the ESP32-S3 board. Look at the metal-shielded module — it should have a label printed on it. Confirm the label says **ESP32-S3-WROOM-1-N16R8**. If it says N4R2 or N8R2, you have the wrong variant — return it.

**Step 2 — Check the headers.**
If the pin headers are already soldered to the board (two rows of pins sticking down), skip to Step 5. If you received loose header strips, continue to Step 3.

**Step 3 — Solder the headers (if loose).**
- **Tool:** Soldering iron set to 320°C (608°F). Use lead-free solder (Sn99.3/Cu0.7) or leaded (Sn63/Pb37, easier to work with).
- Push the long side of the header pins through the board holes from the bottom, so the short side sticks up through the top.
- Place the board upside down on a flat surface so the headers don't fall out.
- Touch the soldering iron tip to where the pin meets the copper pad on the top of the board. Hold for 2 seconds.
- Feed solder into the joint. You need a small shiny cone of solder around each pin.
- Repeat for all 44 pins.
- **Common mistake:** Cold solder joint — the solder looks dull and grainy instead of shiny. Fix: reheat the joint and add a tiny bit more solder.

**Step 4 — Check your solder work.**
Look at each pin from the side. Each joint should be a small shiny cone. No two adjacent pins should be bridged (connected by a blob of solder). If you see a bridge, place the iron between the two pins and drag the excess solder away.

**Step 5 — Insert into breadboard.**
Push the board into the breadboard so it straddles the center channel. The pins should click into the breadboard holes. The USB-C port should face the edge of the breadboard for easy cable access.

**Step 6 — Connect USB-C cable.**
Plug the USB-C cable into the port labelled **USB** (not "UART" — some boards have two ports; use the one closer to the ESP32 module). Plug the other end into your computer.

## First Power-On

**What you should see:**
1. A small red LED near the USB connector lights up — this means power is good.
2. The built-in RGB LED (near pin IO48) may briefly flash — this is the factory test firmware.
3. On your computer, a new serial device should appear.

**Check USB enumeration:**
```bash
# On your WSL2 Debian machine (after USB passthrough via usbipd-win):
ls /dev/ttyACM* /dev/ttyUSB*
# You should see /dev/ttyACM0 or /dev/ttyUSB0
```

**If you do NOT see a serial device:**
- Try a different USB-C cable (some cables are charge-only with no data wires).
- Try the other USB port on the board if it has two.
- On Windows, check Device Manager for "USB Serial Device" or "ESP32-S3".

## Bring-Up Test Program

This minimal program blinks the built-in RGB LED. It proves the board works and your development environment is set up.

**Prerequisites:** Install ESP-IDF (see `scripts/bootstrap/05-esp-idf.sh`).

```cpp
// main/main.cpp — ESP32-S3 blink test
#include <cstdio>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

extern "C" void app_main() {
    printf("ESP32-S3 DevKitC-1 N16R8 — Blink Test\n");

    // Configure the built-in RGB LED on GPIO48
    led_strip_handle_t led_strip;
    led_strip_config_t strip_config = {
        .strip_gpio_num = GPIO_NUM_48,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10'000'000,
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);

    bool on = false;
    while (true) {
        if (on) {
            led_strip_set_pixel(led_strip, 0, 0, 16, 0);  // dim green
        } else {
            led_strip_clear(led_strip);
        }
        led_strip_refresh(led_strip);
        on = !on;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

**Expected output:** The RGB LED on the board blinks green every 500 ms. Serial monitor shows: `ESP32-S3 DevKitC-1 N16R8 — Blink Test`.

## Failure Signatures

| Symptom | Cause | Fix |
|---------|-------|-----|
| No red power LED | Bad USB cable or port | Try another cable (must be data+power). Try another USB port. |
| Power LED on but no serial device | Wrong USB port on board | Use the port labelled "USB", not "UART". |
| Serial device appears then disappears | Board in boot loop (bad firmware) | Hold BOOT button, press RST, release BOOT. This enters download mode. Re-flash. |
| Build fails with "PSRAM not found" | Wrong variant (not N16R8) | Check the module label. Must say N16R8. |
| Pins IO35/36/37 don't work | Used by internal flash/PSRAM | These pins are reserved on N16R8. Use other pins. |

## Datasheet Links

- [ESP32-S3-WROOM-1 Module Datasheet (Espressif official)](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
- [ESP32-S3-DevKitC-1 User Guide (Espressif official)](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/index.html)
- [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)

## Safety Notes

- **No dangerous voltages** on this board — it runs at 3.3V logic, powered by 5V USB.
- **Static sensitive** — avoid touching the metal-shielded module or the pins while standing on carpet. Touch a grounded metal object first.
- **Do not exceed 3.3V on any GPIO pin** — higher voltage will permanently damage the chip.
- **Do not short 5V to any GPIO pin** — use level shifters if connecting to 5V devices.
