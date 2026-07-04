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

On a bare bench board (no switches or beam wired) the gate controller
then boots straight into `Faulted` — by design, not by accident:
unwired normally-open limit inputs read "not at either limit" (unknown
position) and the unwired beam input reads "blocked" (failsafe), so
expect:

```
W (…) gate-ctrl: safety beam blocked at boot — close commands will be rejected
E (…) gate-ctrl: cannot resolve position (open=0 closed=0) — mid-travel: operator must reset
I (…) gate-ctrl: state -> Faulted (reason=LimitSwitchConflict)
I (…) gate-ctrl: started (state=Faulted, reverse_on_beam=1, auto_close=0ms)
I (…) gate-ctrl: heartbeat: state=Faulted reason=LimitSwitchConflict beam=blocked limits[open=0 closed=0]
```

With real hardware attached and the gate parked on its closed limit,
the same sequence lands in `Closed` and the heartbeat reports
`limits[open=0 closed=1]`.

## Layout

```
firmware/
├── CMakeLists.txt          # Top-level project file (calls into ESP-IDF cmake)
├── partitions.csv          # 4-MB OTA-ready partition map
├── sdkconfig.defaults      # Compile-time defaults (target=esp32s3, FreeRTOS,
│                           #   logging, watchdog, partitions)
├── main/                   # app_main() entry component
│   ├── CMakeLists.txt
│   └── main.cpp            # Boot banner, ethernet bring-up, GateController start.
├── host/                   # Host-side static-lib mirror of pure components so
│                           #   the Catch2 suite links them (built by root CMake).
└── components/
    ├── gate_drivers/       # First-party hardware drivers (relays, GPIOs,
    │                       #   W5500 ethernet, safety beam, status LEDs).
    ├── gate_state_machine/ # Pure C++20 gate transition logic — no IDF deps;
    │                       #   unit-tested on host (tests/state_machine/).
    └── gate_control/       # GateController: FreeRTOS event-pump task, motor
                            #   watchdog + auto-close timers, action dispatch.
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
