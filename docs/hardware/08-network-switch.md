# Component Guide 08: PoE Network Switch — Connecting Everything Together

## What This Part Does

This is a small box with multiple Ethernet ports. It connects all the networked devices at the gate (camera, LiDAR, ESP32-S3 W5500) to each other and to the GPU server. It also sends power to the Hikvision camera through the Ethernet cable (Power over Ethernet / PoE), so the camera only needs one cable for both power and data.

## Exact Part

- **Type:** 5-port Gigabit Unmanaged PoE Switch
- **Recommended model:** TP-Link TL-SG1005P (5-port Gigabit, 4× PoE+, 65W total)
- **Key specs:** 5× 10/100/1000 Mbps ports, 4 ports with PoE (802.3af/at), plug-and-play (no configuration)
- **Power:** 240V AC input (built-in power supply)

### Why This Specific Switch?

- 4 PoE ports — one powers the Hikvision camera; spares for future cameras
- Gigabit speed — handles LiDAR point cloud data without bottleneck
- Unmanaged — zero configuration, just plug cables in
- TP-Link is widely available in Nairobi and trusted
- Small enough to mount in the gate enclosure

## Where to Buy

| Source | Approximate Price | Notes |
|--------|-------------------|-------|
| **Luthuli Avenue, Nairobi** — networking shops, BIHI Towers basement | KES 4,500–6,500 | Very widely available. Ask for "TP-Link 5-port PoE switch, the TL-SG1005P." |
| **Jumia Kenya** (jumia.co.ke) | KES 5,000–7,000 | Online order with delivery. |
| **Glantix** (BIHI Towers, Moi Avenue) | KES 5,000–6,000 | Walk-in shop. |
| **AliExpress** — search "TP-Link TL-SG1005P" | USD 30–40 (~KES 3,900–5,200) | Cheaper but 3–4 week shipping. |

**Alternative (budget):** Tenda TEG1105P-4-63W — similar specs, ~KES 2,000–3,000 on Jumia.

**Lead time:** Nairobi: same day. AliExpress: 2–4 weeks.

## What's in the Box

- 1× TP-Link TL-SG1005P switch
- 1× Power cable (240V AC, Kenya Type G plug or Euro plug — verify before buying)
- 1× Mounting brackets and screws (for wall mount)
- 1× Quick start guide

### What You Need Separately

- 4× Ethernet cables, Cat5e or Cat6, lengths as needed:
  - Camera → Switch: depends on installation (1m–50m)
  - LiDAR → Switch: depends on installation (1m–10m)
  - ESP32+W5500 → Switch: short (0.5m–2m for bench testing)
  - Switch → GPU Server: depends on installation (1m–50m)

## Connections

```
  TP-Link TL-SG1005P (5-Port PoE Switch)
  ┌─────────────────────────────────────────────────┐
  │                                                 │
  │  Port 1     Port 2     Port 3     Port 4     Port 5   │
  │  (PoE)     (PoE)     (PoE)     (PoE)     (non-PoE)│
  │    │          │          │          │          │    │
  └────┼──────────┼──────────┼──────────┼──────────┼────┘
       │          │          │          │          │
       │          │          │          │          │
  ┌────┴────┐ ┌───┴───┐ ┌───┴───┐     │    ┌─────┴─────┐
  │Hikvision│ │Unitree│ │ESP32  │   (spare) │GPU Server │
  │ Camera  │ │ L1    │ │+W5500 │          │(RTX 4060+)│
  │(powered │ │LiDAR  │ │       │          │           │
  │ by PoE!)│ │       │ │       │          │           │
  └─────────┘ └───────┘ └───────┘          └───────────┘

  Port assignments (recommended):
  Port 1 (PoE) ── Hikvision camera (powered by PoE through the cable)
  Port 2 (PoE) ── Unitree L1 LiDAR (LiDAR has its own power; PoE not used but won't hurt)
  Port 3 (PoE) ── ESP32-S3 + W5500 (W5500 does NOT support PoE — safe on PoE port, PoE only activates on request)
  Port 4 (PoE) ── Spare (future second camera or second LiDAR)
  Port 5       ── GPU Server (uplink to server or existing network)
```

## Step-by-Step Assembly

### What You Need

- [ ] TP-Link TL-SG1005P PoE switch
- [ ] Power cable (in the box)
- [ ] 4× Ethernet cables (Cat5e/Cat6)
- [ ] All other devices already set up (camera, LiDAR, ESP32+W5500, GPU server)

### Steps

**Step 1 — Place the switch.**
Put the switch on your desk (for testing) or mount it on the wall near the gate (for installation) using the provided brackets. Keep it dry — the switch is NOT waterproof. For outdoor installation, put it inside the field enclosure.

**Step 2 — Plug in the power cable.**
Connect the power cable to the switch and to a 240V outlet. The LEDs on the front should light up (power LED, typically green).

**Step 3 — Connect the Hikvision camera (Port 1).**
Plug the camera's Ethernet cable into **Port 1**. Within 10 seconds, the Port 1 LED should light up green (link) and the PoE LED should light up (power being delivered to the camera). The camera will boot in ~30–60 seconds.

**Step 4 — Connect the Unitree L1 LiDAR (Port 2).**
Plug the LiDAR's Ethernet cable into **Port 2**. The Port 2 LED should light up green. The LiDAR has its own USB-C power — PoE is not used, but the PoE port won't send power unless the device requests it (802.3af negotiation).

**Step 5 — Connect the ESP32-S3 W5500 (Port 3).**
Plug the Ethernet cable from the W5500 module into **Port 3**. The Port 3 LED should light up.

**Step 6 — Connect the GPU server (Port 5).**
Plug an Ethernet cable from the GPU server (or your development machine) into **Port 5**.

**Step 7 — Verify all links.**
Check the front panel LEDs. All connected ports should show a green link LED. If any port LED is off, check the cable at both ends.

**Step 8 — Verify network connectivity.**
From the GPU server, ping all devices:
```bash
ping 192.168.1.10    # Camera (after you set its static IP)
ping 192.168.1.100   # LiDAR (factory default)
ping 192.168.1.20    # ESP32+W5500 (after DHCP or static config)
```
All three should respond with <1ms latency on a local switch.

## First Power-On

**What you should see:**
1. Power LED (green) lights up immediately when plugged in.
2. Port LEDs light up green within seconds as devices are connected.
3. PoE indicator LED (on Port 1) lights up when the camera is connected and negotiates power.

## Failure Signatures

| Symptom | Cause | Fix |
|---------|-------|-----|
| No LEDs at all | No power or bad power cable | Try another outlet. Check the power cable connection. |
| Port LED off after connecting a cable | Bad cable or device not powered | Try another cable. Check that the device on the other end is on. |
| Camera doesn't get PoE power | Camera not on a PoE port, or switch PoE budget exceeded | Ensure camera is on Port 1–4 (PoE). Check switch's total PoE power draw. |
| Devices can't ping each other | Different subnets or IP conflicts | Ensure all devices are on the same 192.168.1.x subnet. Check for duplicate IPs. |
| Slow or dropped connections | Bad cable or half-duplex negotiation | Use Cat5e or better. Try a different port on the switch. |

## Datasheet Links

- [TP-Link TL-SG1005P Product Page](https://www.tp-link.com/en/business-networking/unmanaged-switch/tl-sg1005p/)

## Safety Notes

- **240V AC power input** — the switch plugs into mains power. Handle the power cable with dry hands. Do not use the switch near water unless it's inside a sealed enclosure.
- **PoE ports carry 48V DC** on the Ethernet cable. While not lethal, do not touch bare Ethernet wire ends when PoE ports are active. If you're making custom Ethernet cables, do it with the switch unplugged.
- **For outdoor installation:** Mount the switch inside an IP65-rated enclosure. The switch itself is not weatherproof.
- **Heat:** The switch generates some heat. Ensure adequate ventilation in the enclosure. Operating temperature range is 0°C to 40°C.
