# Bill of Materials — Prototype (Single Gate)

**Date:** 2026-04-20
**Currency:** KES (Kenyan Shilling) and USD. Exchange rate used: 1 USD ≈ 130 KES.

## Gate-Side Components

| # | Component | Model | Qty | Unit Price (KES) | Unit Price (USD) | Total (KES) | Source |
|---|-----------|-------|-----|-------------------|-------------------|-------------|--------|
| 1 | MCU Dev Board | ESP32-S3-DevKitC-1-N16R8 | 1 | 1,500 | 11.50 | 1,500 | AliExpress / Luthuli Ave |
| 2 | Ethernet Module | W5500 SPI Ethernet Module | 1 | 400 | 3.00 | 400 | AliExpress / Luthuli Ave |
| 3 | LiDAR Sensor | Unitree L1 PM (4D, 360°×90°) | 1 | 35,000 | 269.00 | 35,000 | Unitree official / AliExpress |
| 4 | ALPR Camera | Hikvision DS-2CD2043G2-I (2.8mm, 4MP, PoE) | 1 | 10,000 | 77.00 | 10,000 | Luthuli Ave (CCTV shops) |
| 5 | Relay Module | 4-Channel 5V Relay w/ Optocoupler | 1 | 400 | 3.00 | 400 | AliExpress / Luthuli Ave |
| 6 | Safety Beam | Photoelectric beam pair (TX+RX), 12V, IP65 | 1 | 1,200 | 9.00 | 1,200 | AliExpress / Luthuli Ave |
| 7 | Limit Switches | Magnetic reed switch NC (wired) | 2 | 150 | 1.15 | 300 | Luthuli Ave (alarm shops) |
| 8 | PoE Network Switch | TP-Link TL-SG1005P (5-port Gigabit PoE) | 1 | 5,500 | 42.00 | 5,500 | Luthuli Ave / Jumia |
| 9 | Power Supply 12V | 12V/2A AC-DC enclosed PSU | 1 | 600 | 4.60 | 600 | Luthuli Ave |
| 10 | Buck Converter 5V | LM2596 12V→5V DC-DC step-down | 1 | 100 | 0.77 | 100 | AliExpress / Luthuli Ave |
| 11 | Enclosure | IP65 junction box, 200×150×100mm (ABS) | 1 | 1,500 | 11.50 | 1,500 | Luthuli Ave / AliExpress |
| 12 | Ethernet Cables | Cat5e, assorted lengths (1m, 2m, 5m, 10m) | 4 | 200 | 1.50 | 800 | Luthuli Ave |
| 13 | Breadboard | 830-point full-size (for prototyping) | 1 | 300 | 2.30 | 300 | Luthuli Ave |
| 14 | Jumper Wires | Male-to-male, assorted, 40-pack | 1 | 200 | 1.50 | 200 | Luthuli Ave |
| 15 | USB-C Cable | USB-C data+power, 1m | 1 | 200 | 1.50 | 200 | Luthuli Ave |
| 16 | Misc Wire | 18 AWG stranded, 5m red + 5m black | 1 | 300 | 2.30 | 300 | Luthuli Ave |

### Gate-Side Subtotal: KES 58,300 (~USD 448)

## Server-Side Components

| # | Component | Model | Qty | Unit Price (KES) | Unit Price (USD) | Total (KES) | Source |
|---|-----------|-------|-----|-------------------|-------------------|-------------|--------|
| 17 | GPU | NVIDIA GeForce RTX 4060 8GB | 1 | 45,000 | 346.00 | 45,000 | Luthuli Ave / Amazon |
| 18 | Mini PC / Desktop | i5 12th gen+, 16GB RAM, 256GB SSD, PCIe x16 | 1 | 40,000 | 308.00 | 40,000 | Luthuli Ave / Amazon |
| 19 | Ethernet Cable | Cat5e, 2m (server to switch) | 1 | 200 | 1.50 | 200 | Luthuli Ave |

### Server-Side Subtotal: KES 85,200 (~USD 655)

## Gate Motor (if not already installed)

| # | Component | Model | Qty | Unit Price (KES) | Unit Price (USD) | Total (KES) | Source |
|---|-----------|-------|-----|-------------------|-------------------|-------------|--------|
| 20 | Sliding Gate Motor | CENTURION D5-Evo (up to 500kg gate) | 1 | 65,000 | 500.00 | 65,000 | Glantix / Hubtech / Dataworld |

### Motor Subtotal: KES 65,000 (~USD 500)

---

## Totals

| Category | Total (KES) | Total (USD) |
|----------|-------------|-------------|
| Gate-side electronics | 58,300 | 448 |
| GPU server | 85,200 | 655 |
| **System total (without motor)** | **143,500** | **1,103** |
| Gate motor (if needed) | 65,000 | 500 |
| **Grand total (with motor)** | **208,500** | **1,603** |

## Notes

1. Prices are approximate as of April 2026 and will vary by seller and availability.
2. The gate motor is only needed if the site doesn't already have one installed. Most Kenyan residential estates already have a CENTURION or ET motor.
3. For a **dual-leaf swing gate**, replace the D5-Evo (sliding) with a CENTURION R5 swing motor (~KES 55,000–70,000) — the interface wiring is the same.
4. Tools (soldering iron, multimeter, drill, screwdrivers) are not included — assumed available. Budget ~KES 5,000 for basic tools if starting from zero.
5. The server can serve multiple gates — for a multi-gate estate, only the gate-side cost repeats per gate.
