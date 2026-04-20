# Component Guide 03: Unitree L1 4D LiDAR — Vehicle Detection Sensor

## What This Part Does

This sensor shoots invisible laser beams in a dome pattern (360° around, 90° up) and measures how far away everything is. It creates a 3D "point cloud" — a map of dots showing where objects are. We use it to detect when a vehicle enters the gate zone, classify whether it's a car, SUV, or truck, and monitor the gate opening for safety (stop if something is in the way).

## Exact Part

- **Manufacturer:** Unitree Robotics
- **Model:** Unitree 4D LiDAR L1
- **Variant:** L1 **PM** (Proximity Measurement, 20m range — NOT the L1 RM which is more expensive)
- **Key specs:** 360° × 90° FOV, 21,600 points/sec, 0.05–30m range, IMU built-in, Ethernet + UART
- **Size:** 75 × 75 × 65 mm, 230g
- **Laser class:** Class 1 (eye-safe — safe to look at)

## Where to Buy

| Source | Approximate Price | Notes |
|--------|-------------------|-------|
| **Unitree official store** (shop.unitree.com) | USD 249 (~KES 32,400) | Ships worldwide. Most reliable source. |
| **AliExpress** — search "Unitree L1 LiDAR" | USD 230–280 (~KES 30,000–36,500) | Check seller reviews. Ensure it's the genuine Unitree L1. |
| **Amazon** — search "Unitree 4D LiDAR L1 PM" | USD 280–350 | Faster shipping but more expensive. |
| **RobotShop** (robotshop.com) | USD 280–300 | Robotics specialist retailer. |

**Lead time:** Unitree direct: 1–3 weeks. AliExpress: 2–4 weeks.

**Not available in Nairobi shops** — this is a specialty robotics part. Order online.

## What's in the Box

- 1× Unitree L1 LiDAR unit (black cylinder with rotating top)
- 1× Ethernet cable (short, ~0.5m)
- 1× Serial cable (for UART connection — we'll use Ethernet instead)
- 1× USB-C power cable
- 1× Mounting bracket and screws
- 1× Quick start guide

### What You Need to Buy Separately

- 1× Ethernet cable, Cat5e/Cat6, length as needed for your installation (1m for bench, up to 50m for field)
- 1× 5V/2A USB-C power supply (or power from a USB power bank for testing)
- 1× Network switch (to connect LiDAR + W5500 ESP32 + GPU server — see Guide 08)

## Pinout and Connections

```
  Unitree L1 LiDAR
  ┌──────────────────┐
  │   ┌──────────┐   │
  │   │ Rotating │   │
  │   │  sensor  │   │
  │   │   head   │   │
  │   └──────────┘   │
  │                  │
  │  Ethernet port ──┼──→ To network switch (same switch as ESP32 W5500)
  │  USB-C power  ──┼──→ 5V/2A USB-C power supply
  │  Serial port  ──┼──→ Not used (Ethernet is faster)
  └──────────────────┘

  Network topology:
  ┌─────────┐    ┌──────────┐    ┌─────────────┐
  │ Unitree │    │ Network  │    │ GPU Server  │
  │ L1      ├────┤ Switch   ├────┤ (RTX 4060+) │
  │ LiDAR   │    │ (5-port) │    │             │
  └─────────┘    │          │    └─────────────┘
                 │          │
  ┌─────────┐   │          │
  │ ESP32-S3├───┤          │
  │ + W5500 │   └──────────┘
  └─────────┘
```

### Default Network Settings (from factory)

| Parameter | Value |
|-----------|-------|
| LiDAR IP | 192.168.1.100 |
| Point cloud port | 6001 (UDP) |
| IMU port | 6002 (UDP) |
| Config port | 6003 (TCP) |

You may need to change these to match your network. The Unitree SDK includes a configuration tool.

## Step-by-Step Assembly

### What You Need

- [ ] Unitree L1 LiDAR
- [ ] Ethernet cable (from box or your own)
- [ ] USB-C power cable and 5V/2A power supply
- [ ] Network switch (already connected to ESP32 W5500 and GPU server)
- [ ] Computer with the Unitree SDK installed (for initial test)

### Steps

**Step 1 — Unbox and inspect.**
Open the box. You should see the black cylindrical LiDAR unit. The top part rotates — this is normal. Do NOT hold it by the rotating head. Always hold it by the base.

**Step 2 — Mount the bracket (for testing, just set it on a desk).**
For bench testing, place the LiDAR on a flat desk surface pointing upward. For field installation, mount the bracket on the gate pillar or overhead beam using the provided screws (M3). The ideal mounting position is 2–3 meters above ground, looking down at the gate approach lane.

**Step 3 — Connect Ethernet.**
Plug one end of the Ethernet cable into the LiDAR's Ethernet port. Plug the other end into your network switch.

**Step 4 — Connect power.**
Plug the USB-C power cable into the LiDAR's USB-C port. Plug the other end into a 5V/2A USB-C power supply or a USB power bank.

**Step 5 — Power on.**
The LiDAR starts automatically when power is connected. There is no power button.

## First Power-On

**What you should see:**
1. Within 2 seconds: a faint whirring sound — this is the motor spinning up the rotating head.
2. Within 5 seconds: the head reaches full speed (the whirring becomes a steady hum).
3. The Ethernet LEDs on the LiDAR port should show link activity.

**What you should hear:**
- A quiet, steady hum. If you hear grinding, clicking, or irregular noises, the motor bearing may be damaged — contact Unitree support.

**Verify on your computer:**
```bash
# Set your computer's Ethernet interface to the same subnet
# (the LiDAR defaults to 192.168.1.100)
sudo ip addr add 192.168.1.50/24 dev eth0

# Ping the LiDAR
ping 192.168.1.100
# Expected: replies with <1ms latency

# Listen for point cloud data (UDP port 6001)
sudo tcpdump -i eth0 udp port 6001 -c 5
# Expected: packets arriving at ~20Hz
```

## Bring-Up Test Program

This C++ program connects to the L1 via the Unitree SDK, receives point cloud frames, and prints statistics.

**Prerequisites:** Clone and build the [Unitree LiDAR SDK](https://github.com/unitreerobotics/unilidar_sdk).

```cpp
// test_lidar.cpp — Unitree L1 bring-up test
#include <cstdio>
#include <unitree_lidar_sdk.h>

int main() {
    printf("Unitree L1 LiDAR Bring-Up Test\n");

    auto* lidar = createUnitreeLidarReader();

    // Connect via Ethernet (default: 192.168.1.100, port 6001)
    if (lidar->initializeEthernet("192.168.1.100", 6001) != 0) {
        printf("ERROR: Cannot connect to LiDAR at 192.168.1.100:6001\n");
        printf("  - Check Ethernet cable\n");
        printf("  - Check that your computer is on the 192.168.1.x subnet\n");
        printf("  - Check that the LiDAR has power (spinning head)\n");
        return 1;
    }

    printf("Connected! Receiving point clouds...\n");

    for (int frame = 0; frame < 10; ++frame) {
        auto cloud = lidar->getCloud();
        if (cloud.points.empty()) {
            printf("Frame %d: no points (sensor may still be starting)\n", frame);
        } else {
            printf("Frame %d: %zu points, range %.2f–%.2f m\n",
                   frame, cloud.points.size(),
                   cloud.min_range, cloud.max_range);
        }
    }

    printf("\nLiDAR test PASSED!\n");
    delete lidar;
    return 0;
}
```

**Expected output:**
```
Unitree L1 LiDAR Bring-Up Test
Connected! Receiving point clouds...
Frame 0: 2160 points, range 0.12–8.45 m
Frame 1: 2160 points, range 0.11–8.52 m
...
LiDAR test PASSED!
```

## Failure Signatures

| Symptom | Cause | Fix |
|---------|-------|-----|
| No sound (head not spinning) | No power or bad USB-C cable | Try another cable/power supply. Must be 5V/2A minimum. |
| Head spins but no Ethernet link LEDs | Bad Ethernet cable or wrong port | Try another cable. Make sure switch port is active. |
| Ping fails to 192.168.1.100 | Computer not on same subnet | Run `sudo ip addr add 192.168.1.50/24 dev eth0`. |
| Points received but all at 0 range | Lens blocked or sensor fault | Clean the lens with a microfiber cloth. If still zero, contact Unitree. |
| Intermittent point cloud gaps | Ethernet congestion or bad cable | Use Cat5e or better. Dedicated switch port for LiDAR. |

## Datasheet Links

- [Unitree L1 Product Page (official)](https://www.unitree.com/LiDAR/)
- [Unitree L1 User Manual PDF](https://oss-global-cdn.unitree.com/static/52b72f707b304d229d4321eea223738f.pdf)
- [Unitree LiDAR SDK (GitHub)](https://github.com/unitreerobotics/unilidar_sdk)

## Safety Notes

- **Laser Class 1** — eye-safe under all normal operating conditions. You can look at the sensor while it's running without eye protection.
- **Rotating parts** — the sensor head spins at high speed. Do NOT insert fingers, tools, or cables into the gap between the head and the base while it's running.
- **Mounting height** — when mounted 2–3m above ground, ensure the bracket is secure. A falling LiDAR unit could injure someone below. Use lock washers or thread-locking compound on mounting screws.
- **Weather** — the L1 is rated IP54 (splash-resistant, not waterproof). For outdoor installation, mount it under an overhang or in a weatherproof enclosure with a clear window.
