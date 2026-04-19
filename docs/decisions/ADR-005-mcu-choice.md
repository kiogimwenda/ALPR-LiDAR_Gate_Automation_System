# ADR-005: MCU / SoC Choice for Field PCB — ESP32-S3

## Status
Accepted

## Date
2026-04-19

## Context

The field PCB sits at the gate and handles: camera frame capture, LiDAR data acquisition, gate relay control, local SQLite logging, RPC communication with the GPU server over Ethernet, watchdog, and OTA updates. It does **not** run inference — that happens on the GPU server.

Key requirements:
- Ethernet (wired LAN to GPU server)
- USB Host (for camera and/or LiDAR)
- GPIO for relay control, limit switches, safety sensors
- Enough RAM for frame buffering (~2-4 MB for a 1080p frame)
- WiFi/BLE optional (for initial setup, not primary data path)
- Available in Nairobi and on AliExpress at reasonable cost
- Good toolchain (IDE, debugger, OTA framework)

### Options Evaluated

1. **ESP32-S3** (Espressif, Xtensa LX7 dual-core, 240 MHz)
   - 512 KB SRAM + up to 8 MB PSRAM
   - USB OTG (host + device)
   - WiFi + BLE 5.0
   - Ethernet via SPI (W5500) or RMII (LAN8720)
   - ESP-IDF mature framework with OTA, NVS, HTTPS client
   - ~KES 800-1,500 in Nairobi, ~USD 3-6 on AliExpress
   - Large community, extensive documentation

2. **STM32H7** (ST, Cortex-M7 @ 480 MHz)
   - Up to 1 MB SRAM, external SDRAM possible
   - Built-in Ethernet MAC (needs PHY)
   - USB OTG
   - More powerful CPU, better for heavy local processing
   - ~KES 2,000-4,000, harder to source in Nairobi
   - STM32CubeIDE, HAL libraries — steeper learning curve
   - No WiFi/BLE built-in

3. **Raspberry Pi CM4 / CM5** (Broadcom, Cortex-A)
   - Full Linux — can run the entire firmware as a userspace app
   - Built-in Ethernet, USB, GPIO
   - Runs SQLite, gRPC, systemd natively
   - ~KES 8,000-15,000, availability fluctuates
   - Not a true MCU — higher power consumption, longer boot time
   - Overkill if inference is offloaded

### Ranking

**Option 1 (ESP32-S3) is best for prototype** because:
- Lowest cost and best availability in Nairobi
- ESP-IDF provides OTA, HTTPS, and NVS out of the box
- USB OTG can interface with USB cameras
- Ethernet via W5500 SPI module is reliable and cheap
- 8 MB PSRAM is sufficient for frame buffering when frames are streamed to the GPU server immediately
- Community support makes debugging easier for a first hardware revision

**Option 3 (RPi CM4/CM5) is the fallback** for production if the ESP32-S3's RAM or USB bandwidth proves insufficient for simultaneous camera + LiDAR + Ethernet — but at 5-10x the cost.

**Option 2 (STM32H7) is the fallback** if we need bare-metal real-time guarantees for gate safety logic — but ESP-IDF's FreeRTOS provides adequate real-time behavior for gate timing.

## Decision

Use **ESP32-S3-WROOM-1** (8 MB PSRAM variant) with **W5500 Ethernet module** for the prototype field PCB. Firmware developed with **ESP-IDF** (latest stable). Evaluate upgrading to RPi CM5 for production if prototype testing reveals limitations.

## Consequences

- Firmware written in C++ on ESP-IDF (FreeRTOS underneath)
- Ethernet via SPI (W5500) — max ~10 Mbps effective, sufficient for compressed frames
- Camera connected via USB OTG or dedicated CSI-to-USB bridge
- LiDAR connected via Ethernet (separate port on a switch) or UART depending on sensor choice
- PCB designed in KiCad with ESP32-S3 module, W5500, relay drivers, power regulation
- Install ESP-IDF toolchain in bootstrap phase
