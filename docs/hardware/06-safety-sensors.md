# Component Guide 06: Photoelectric Beam Sensor + Limit Switches — Safety and Position

## What These Parts Do

**Photoelectric beam sensor:** Two units (a transmitter and a receiver) placed on opposite sides of the gate opening. They shoot an invisible infrared beam across the gap. If a person, animal, or vehicle breaks the beam while the gate is closing, the system immediately stops and reverses the gate. This prevents the gate from crushing someone.

**Limit switches:** Small magnetic switches mounted at the gate's fully-open and fully-closed positions. They tell the ESP32 exactly when the gate has reached the end of its travel in either direction.

---

## Part A: Photoelectric Beam Sensor

### Exact Part

- **Type:** Through-beam photoelectric sensor pair (TX + RX)
- **Recommended model:** Any gate-compatible photocell pair — commonly sold as "gate safety photocell" or "IR beam sensor"
- **Key specs:** 12–24V DC, IP65 weather-rated, detection range 10–20m, NO/NC dry contact output
- **Look for:** Pairs specifically sold for gate automation. Popular brands: TOPENS, ALEKO, CAME, BFT.

### Where to Buy

| Source | Approximate Price | Notes |
|--------|-------------------|-------|
| **AliExpress** — search "gate safety photocell infrared beam IP65" | USD 5–12 (~KES 650–1,560) | Very common. Get the pair (TX + RX). |
| **Luthuli Avenue, Nairobi** — gate motor shops, CCTV shops | KES 800–1,500 | Ask for "safety beam for gate motor, the pair." Often sold alongside CENTURION motors. |
| **Amazon** — search "TOPENS TC102 photocell gate" | USD 12–18 | TOPENS TC102 is reliable and well-documented. |

### What's in the Box

- 1× Transmitter unit (TX — has one lens)
- 1× Receiver unit (RX — has one lens + output wires)
- 2× Mounting brackets with screws
- Wiring instructions

### What You Need Separately

- 12V DC power supply (can share with other 12V components)
- 2-conductor wire, 18 AWG (~1.0mm²), length as needed (run from receiver to ESP32 location)

### Wiring

```
  12V Power Supply
       │
       ├──── (+) ──→ TX unit VCC (+)
       │              TX unit GND (-) ──→ GND rail
       │
       ├──── (+) ──→ RX unit VCC (+)
       │              RX unit GND (-) ──→ GND rail
       │
       │              RX unit RELAY OUTPUT:
       │              ┌────────────────┐
       │              │ COM ───────────┼──→ ESP32-S3 IO15 (pulled up internally)
       │              │ NC  ───────────┼──→ ESP32-S3 GND
       │              └────────────────┘
       │
  GND rail ────────── ESP32-S3 GND (common ground)

  How it works:
  - Beam clear (no obstruction): NC contact is CLOSED → IO15 sees GND → reads LOW
  - Beam broken (obstruction!):  NC contact OPENS   → IO15 pulled HIGH → reads HIGH
  - In firmware: HIGH on IO15 = OBSTRUCTION DETECTED = STOP GATE IMMEDIATELY
```

### Installation Steps

**Step 1 — Choose mounting positions.**
Mount the TX on one gate pillar and the RX on the opposite pillar, at 30–40cm height (knee level). They must face each other directly across the gate opening. The beam should cross the gate path at a point where the gate panels travel.

**Step 2 — Mount the brackets.**
Use the provided screws and wall anchors to mount the brackets on the gate pillars. Use a drill with a 6mm masonry bit if mounting on concrete/brick pillars. For metal pillars, use self-tapping screws.

**Step 3 — Snap the sensors into the brackets.**
The sensor units clip or screw onto the brackets. Aim them at each other — the lenses should face each other across the gap.

**Step 4 — Wire power (12V).**
Connect +12V and GND to both the TX and RX units. Use the screw terminals on the back of each unit. Tighten firmly.

**Step 5 — Wire the output.**
From the RX unit's output terminals, run two wires back to the ESP32-S3:
- RX **COM** terminal → ESP32-S3 **IO15**
- RX **NC** terminal → ESP32-S3 **GND**

**Step 6 — Align the beam.**
With power on, the RX unit typically has an indicator LED. When the beam is aligned:
- LED is ON (beam received) = all clear
- LED is OFF (beam blocked) = obstruction or misaligned

Adjust the angle of both TX and RX until the RX LED is steadily ON. Then tighten the bracket screws.

**Step 7 — Test by blocking the beam.**
Walk through the gate opening. The RX LED should turn OFF when your body breaks the beam, and turn back ON when you move away.

---

## Part B: Magnetic Reed Limit Switches

### Exact Part

- **Type:** Magnetic reed switch (surface mount, NC — Normally Closed)
- **Recommended:** Generic wired magnetic door/window sensor, NC contact
- **Key specs:** NC contact, adhesive or screw mount, rated for outdoor use
- **Wire length:** At least 50cm pigtail (you may need to extend)

These are the same sensors used on doors and windows for burglar alarms — extremely common and cheap.

### Where to Buy

| Source | Approximate Price | Notes |
|--------|-------------------|-------|
| **AliExpress** — search "magnetic reed switch NC wired" | USD 0.50–1.50 (~KES 65–200) each | Buy at least 4 (2 needed + 2 spares). |
| **Luthuli Avenue, Nairobi** — any alarm/security shop | KES 100–200 each | Ask for "magnetic door sensor, the wired type, normally closed." |
| **Amazon** — search "wired magnetic door contact NC" | USD 2–5 per pair | Usually sold in packs of 5–10. |

You need **2 units**: one for the OPEN position, one for the CLOSED position.

### How They Work

```
  Each reed switch has two parts:
  ┌──────────┐   ┌──────────┐
  │  SWITCH  │   │  MAGNET  │
  │ (wired)  │   │ (no wire)│
  └──────────┘   └──────────┘
       ↑               ↑
  Mounted on the    Mounted on the
  gate frame         gate panel
  (doesn't move)    (moves with gate)

  When the magnet is close to the switch (< 15mm): switch is CLOSED (NC)
  When the magnet moves away from the switch: switch OPENS

  For CLOSED position limit switch:
  - Gate fully closed → magnet next to switch → switch closed → IO7 reads LOW
  - Gate opens → magnet moves away → switch opens → IO7 reads HIGH (pull-up)

  For OPEN position limit switch:
  - Gate fully open → magnet next to switch → switch closed → IO6 reads LOW
  - Gate closing → magnet moves away → switch opens → IO6 reads HIGH (pull-up)
```

### Wiring

```
  Limit Switch OPEN position:
  ┌───────────────┐
  │ Reed Switch   │
  │ Wire 1 ───────┼──→ ESP32-S3 IO6 (internal pull-up enabled)
  │ Wire 2 ───────┼──→ ESP32-S3 GND
  └───────────────┘

  Limit Switch CLOSED position:
  ┌───────────────┐
  │ Reed Switch   │
  │ Wire 1 ───────┼──→ ESP32-S3 IO7 (internal pull-up enabled)
  │ Wire 2 ───────┼──→ ESP32-S3 GND
  └───────────────┘

  Reading the state in firmware:
  IO6 == LOW  → gate is fully OPEN
  IO6 == HIGH → gate is not fully open (moving or closed)
  IO7 == LOW  → gate is fully CLOSED
  IO7 == HIGH → gate is not fully closed (moving or open)
```

### Installation Steps

**Step 1 — Identify the fully-open and fully-closed positions.**
Manually move the gate to the fully-closed position. Mark where the edge of the gate panel meets the gate frame. This is where you'll mount the CLOSED limit switch.

**Step 2 — Mount the CLOSED limit switch.**
Peel the adhesive backing off the reed switch (the wired part) and stick it on the gate frame at the marked position. Peel the magnet's adhesive and stick it on the gate panel, directly opposite the switch, within 10mm gap.

**Step 3 — Test the CLOSED limit switch.**
With the gate fully closed, the magnet should be next to the switch. Check continuity with a multimeter: touch the probes to the two wires. You should see ~0Ω (closed circuit). Then manually open the gate by 20cm — the multimeter should show OL (open circuit).

**Step 4 — Mount the OPEN limit switch.**
Move the gate to the fully-open position. Mount the second reed switch on the frame and its magnet on the gate panel at this position, same as Step 2.

**Step 5 — Test the OPEN limit switch.**
Same as Step 3, but at the fully-open position.

**Step 6 — Run wires.**
Run the switch wires back to the ESP32 location. Use cable clips or conduit to secure the wires along the gate frame. Leave some slack where the wire crosses from the moving gate panel to the fixed frame.

## Bring-Up Test Program (Combined)

```cpp
// test_safety.cpp — Test photobeam and limit switches
#include <cstdio>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
    printf("Safety Sensor Test\n");

    // Configure inputs with internal pull-ups
    gpio_set_direction(GPIO_NUM_6, GPIO_MODE_INPUT);   // limit: OPEN
    gpio_set_pull_mode(GPIO_NUM_6, GPIO_PULLUP_ONLY);

    gpio_set_direction(GPIO_NUM_7, GPIO_MODE_INPUT);   // limit: CLOSED
    gpio_set_pull_mode(GPIO_NUM_7, GPIO_PULLUP_ONLY);

    gpio_set_direction(GPIO_NUM_15, GPIO_MODE_INPUT);  // safety beam
    gpio_set_pull_mode(GPIO_NUM_15, GPIO_PULLUP_ONLY);

    while (true) {
        int open_sw   = gpio_get_level(GPIO_NUM_6);
        int closed_sw = gpio_get_level(GPIO_NUM_7);
        int beam      = gpio_get_level(GPIO_NUM_15);

        printf("OPEN=%s  CLOSED=%s  BEAM=%s\n",
               open_sw == 0 ? "YES" : "no ",
               closed_sw == 0 ? "YES" : "no ",
               beam == 1 ? "BLOCKED!" : "clear   ");

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

**Expected output with gate closed, beam clear:**
```
OPEN=no   CLOSED=YES  BEAM=clear
```

## Failure Signatures

| Symptom | Cause | Fix |
|---------|-------|-----|
| Beam sensor always shows "blocked" | Misalignment or dirty lens | Re-aim TX and RX. Clean lenses with dry cloth. |
| Beam sensor always shows "clear" | Wired to NO instead of NC contact | Swap to the NC terminal on the RX unit. |
| Limit switch never triggers | Magnet too far from switch (>15mm) | Move magnet closer to switch. Gap must be <10mm. |
| Limit switch triggers randomly | Vibration or magnet too close to edge | Re-position magnet so it's centered on the switch, not at the edge. |
| IO reads wrong values | Missing pull-up or wrong pin | Verify `GPIO_PULLUP_ONLY` is set. Check pin numbers. |

## Safety Notes

- **The photoelectric beam is a critical safety device.** If it fails, the gate could close on a person or vehicle. The firmware must treat a beam sensor fault (stuck blocked or stuck clear for >60 seconds) as a FAULT condition and stop the gate.
- **Test the beam sensor regularly** — walk through the gate while it's closing and verify it stops and reverses.
- **Limit switches prevent the motor from over-driving** the gate past its travel limits. Without them, the motor could damage the gate track, the gate itself, or the motor.
- **For production: use industrial-grade sensors** (e.g., CAME DIR10 or BFT DESME A15) rather than generic modules. They have better alignment tolerance and longer lifespan.
