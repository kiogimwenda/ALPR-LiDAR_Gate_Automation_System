# gate-firmware

ESP32-S3 firmware for the field-side gate controller. Built with
ESP-IDF v6.x; full sub-milestone plan in
[`README.md`](../README.md#phase-4-sub-milestones-current).

## Quick start

```bash
# 1. One-time: install ESP-IDF (skip if you already have it)
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32s3

# 2. Bring tools onto PATH (do this in every new shell)
. ~/esp/esp-idf/export.sh

# 3. Build (out-of-tree at firmware/build/)
cd <repo>/firmware
idf.py set-target esp32s3
idf.py build

# 4. Flash + open the serial monitor (requires the board attached)
idf.py -p /dev/ttyUSB0 flash monitor
```

Expected first-boot console output:

```
I (303) gate-fw: gate-firmware 0.0.1 starting (idf v6.1.0)
I (303) gate-fw: chip: esp32s3 rev 0.2, 2 cores, WiFi BLE embedded-flash
```

## Layout

```
firmware/
├── CMakeLists.txt          # Top-level project file (calls into ESP-IDF cmake)
├── partitions.csv          # 4-MB OTA-ready partition map
├── sdkconfig.defaults      # Compile-time defaults (target=esp32s3, FreeRTOS,
│                           #   logging, watchdog, partitions)
├── main/                   # app_main() entry component
│   ├── CMakeLists.txt
│   └── main.cpp            # Boot banner + idle task; replaced by Phase 4.5.
└── components/
    └── gate_drivers/       # First-party hardware drivers (relays, GPIOs,
                            #   W5500 ethernet, safety beam, LEDs).
```

## Build target

The standard target is **ESP32-S3** with a 4 MB flash dev kit. Production
hardware is the same chip with 8 MB flash; the partition table works on
both — it leaves the upper half unallocated on 8 MB modules so a future
sub-milestone can carve out a larger SPIFFS for offline-mode caching
without disturbing the OTA layout.

## Sub-milestone status

See the project root [`README.md`](../README.md#phase-4-sub-milestones-current)
for the per-sub-milestone state. The milestone columns marked Phase 4.4
are this directory's roadmap.
