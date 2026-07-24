# Hardware Build Book

**ALPR + LiDAR Automated Vehicle Gate Control System**
**Revision:** 1.0 — Prototype
**Date:** 2026-04-20

---

## Overview

This book walks you through assembling the complete gate control hardware from unboxing to first power-on. Follow the guides in order — each one builds on the previous.

### System Architecture (Physical)

```
  ┌─────────── AT THE GATE ──────────────────────────────────────────┐
  │                                                                   │
  │  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌───────────┐  │
  │  │ Hikvision│    │ Unitree  │    │ Safety   │    │ Limit     │  │
  │  │ Camera   │    │ L1 LiDAR │    │ Beam     │    │ Switches  │  │
  │  │ (4MP,IR) │    │ (3D,360°)│    │ (TX+RX)  │    │ (×2)      │  │
  │  └────┬─────┘    └────┬─────┘    └────┬─────┘    └────┬──────┘  │
  │  Ethernet(PoE)   Ethernet        12V+signal      GPIO wires     │
  │       │               │               │               │          │
  │  ┌────┴───────────────┴───────────────┴───────────────┴────────┐ │
  │  │                    PoE NETWORK SWITCH                       │ │
  │  │                  (TP-Link TL-SG1005P)                       │ │
  │  └────┬────────────────────────┬───────────────────────────────┘ │
  │       │                        │                                  │
  │  ┌────┴─────────────┐    ┌────┴──────────┐                      │
  │  │ ESP32-S3 + W5500 │    │               │                      │
  │  │ (Field PCB)      │    │   Ethernet    │                      │
  │  │ + Relay Module   │    │   cable to    │                      │
  │  │ + 12V/5V PSU     │    │   server      │                      │
  │  └──────┬───────────┘    │   room        │                      │
  │         │                │               │                      │
  │    Relay wires           │               │                      │
  │         │                │               │                      │
  │  ┌──────┴───────────┐   │               │                      │
  │  │ Gate Motor        │   │               │                      │
  │  │ (CENTURION D5/R5) │   │               │                      │
  │  └──────────────────┘   │               │                      │
  │                          │               │                      │
  └──────────────────────────┼───────────────┘──────────────────────┘
                             │
                        Ethernet cable
                        (up to 100m)
                             │
  ┌─────────── SERVER ROOM ──┴────────────────────────────────────┐
  │                                                                │
  │  ┌─────────────────────────────────┐                          │
  │  │ GPU Server                      │                          │
  │  │ (i5/i7 + RTX 4060 8GB)         │                          │
  │  │                                  │                          │
  │  │  Runs: TensorRT inference        │                          │
  │  │        gRPC service              │                          │
  │  │        Dashboard (Drogon)        │                          │
  │  │        OTA update server         │                          │
  │  └─────────────────────────────────┘                          │
  │                                                                │
  └────────────────────────────────────────────────────────────────┘
```

## Build Order

Follow these guides in sequence. Each is self-contained with its own parts list, wiring, and test program.

| Step | Guide | What You Build | Time |
|------|-------|----------------|------|
| 1 | [01-esp32-s3-devkit.md](01-esp32-s3-devkit.md) | Mount ESP32-S3 on breadboard, verify USB + blink LED | 15 min |
| 2 | [02-w5500-ethernet.md](02-w5500-ethernet.md) | Connect W5500 Ethernet, verify DHCP + IP address | 20 min |
| 3 | [05-relay-module.md](05-relay-module.md) | Connect relay module, verify click test | 10 min |
| 4 | [06-safety-sensors.md](06-safety-sensors.md) | Install photobeam + limit switches, verify inputs | 30 min |
| 5 | [07-power-supply.md](07-power-supply.md) | Set up 12V + 5V power supply, disconnect USB | 20 min |
| 6 | [08-network-switch.md](08-network-switch.md) | Connect all devices to PoE switch | 10 min |
| 7 | [04-hikvision-camera.md](04-hikvision-camera.md) | Connect camera via PoE, configure RTSP stream | 20 min |
| 8 | [03-unitree-l1-lidar.md](03-unitree-l1-lidar.md) | Connect LiDAR, verify point cloud data | 20 min |
| 9 | [09-gpu-server.md](09-gpu-server.md) | Set up GPU server, install stack, run smoke test | 60 min |

**Total estimated bench setup time: ~3.5 hours** (assuming all parts are on hand).

## Custom PCB Design (Production)

Once the breadboard prototype is verified, the gate-side electronics can be migrated to a custom 4-layer PCB. See [10-pcb-design-kicad10.pdf](10-pcb-design-kicad10.pdf) for the complete KiCad 10.0 design guide (rev 3.7 — one section per module: footprints, schematic, and direct pin-to-pin connections together, with every capacitor's type stated explicitly, verified KiCad symbol/footprint pairings (RJ45, USB-C), the RJ45's actual internal pinout with correct active-low LED polarity, a single unambiguous USB-C signal-flow diagram, and programming/debug header wiring now documented; pin map synchronized with firmware v1.0.0) covering schematic capture, footprint selection, layout, DRC, and JLCPCB ordering. Estimated cost: USD 120–180 for 5 assembled boards.

## Bill of Materials

See [hardware/bom/prototype-bom.md](../../hardware/bom/prototype-bom.md) for the complete parts list with pricing.

**Summary:**
- Gate-side electronics: **KES 58,300** (~USD 448)
- GPU server: **KES 85,200** (~USD 655)
- Gate motor (if needed): **KES 65,000** (~USD 500)
- **Total: KES 143,500–208,500** (~USD 1,103–1,603)

## Tools Required

| Tool | What For | Approximate Price (KES) |
|------|----------|------------------------|
| Soldering iron (temperature-controlled, 60W) | Soldering headers if needed | 1,500–3,000 |
| Solder wire (0.8mm, Sn63/Pb37 or lead-free) | Soldering | 200–400 |
| Multimeter (digital, basic) | Measuring voltage, checking continuity | 500–1,500 |
| Wire strippers | Stripping wire insulation | 300–600 |
| Small flathead screwdriver (2mm blade) | Screw terminals on relay module and PSU | 100–200 |
| Drill + 6mm masonry bit | Mounting brackets for camera, LiDAR, beam sensor | 2,000–4,000 |
| Cable ties (assorted) | Wire management | 100–200 |

Most of these are available at Luthuli Avenue hardware shops.

## Network IP Address Plan

| Device | IP Address | Port on Switch |
|--------|-----------|---------------|
| Hikvision Camera | 192.168.1.10 | Port 1 (PoE) |
| Unitree L1 LiDAR | 192.168.1.100 | Port 2 |
| ESP32-S3 + W5500 | 192.168.1.20 | Port 3 |
| GPU Server | 192.168.1.50 | Port 5 |
| Gateway/Router (if any) | 192.168.1.1 | — |

Subnet: 192.168.1.0/24

## Verification Checklist

After completing all 9 guides, verify end-to-end connectivity:

- [ ] ESP32-S3 blinks LED and prints to serial
- [ ] W5500 gets IP address via DHCP (or responds to ping with static IP)
- [ ] Relay module clicks on command from ESP32
- [ ] Safety beam: blocking the beam changes IO15 reading
- [ ] Limit switches: triggering each switch changes IO6/IO7 reading
- [ ] 12V and 5V power supplies deliver correct voltages (measured with multimeter)
- [ ] Camera: RTSP stream opens in ffplay or OpenCV on GPU server
- [ ] LiDAR: point cloud data received via Unitree SDK on GPU server
- [ ] GPU server: smoke test passes (CUDA + OpenCV CUDA + TensorRT)
- [ ] All devices can ping each other through the PoE switch

Once all boxes are checked, the hardware is ready for firmware development (Phase 4).
