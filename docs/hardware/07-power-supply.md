# Component Guide 07: Power Supply — Powering the Field PCB

## What This Part Does

This module converts mains electricity (240V AC in Kenya) into safe low voltages (5V and 12V DC) to power all the components at the gate: the ESP32-S3, W5500 Ethernet, relay module, safety beam sensors, and status LEDs. It also provides a 12V line for future RS-485 transceivers or other accessories.

## Exact Parts

You need TWO power supply components:

### Part A: 12V/2A DIN-Rail or Enclosed Power Supply

- **Type:** AC-DC switching power supply, enclosed or DIN-rail mount
- **Input:** 100–240V AC, 50/60Hz (works with Kenya's 240V mains)
- **Output:** 12V DC, 2A (24W)
- **Recommended model:** Mean Well HDR-30-12 (DIN-rail) or any generic 12V/2A enclosed PSU

### Part B: 12V-to-5V Buck Converter (Step-Down Module)

- **Type:** DC-DC step-down module
- **Input:** 7–28V DC
- **Output:** 5V DC, 3A
- **Recommended model:** LM2596-based module (extremely common) or MP1584-based (smaller)
- **Purpose:** Converts the 12V from Part A into 5V for the ESP32-S3, relay module, and W5500.

### Why Not a Single 5V Supply?

The safety beam sensors need 12V. Having both 12V and 5V available gives flexibility for future accessories. The 12V-to-5V buck converter costs less than KES 100 and is more reliable than running two separate power supplies.

## Where to Buy

| Part | Source | Approximate Price |
|------|--------|-------------------|
| 12V/2A PSU | **Luthuli Avenue** — any electronics or LED shop | KES 400–800 |
| 12V/2A PSU | **AliExpress** — "12V 2A switching power supply enclosed" | USD 3–6 (~KES 400–780) |
| 5V buck converter | **Luthuli Avenue** — Nuvision, Kemu | KES 80–150 |
| 5V buck converter | **AliExpress** — "LM2596 DC-DC step down 5V" | USD 0.50–1.50 (~KES 65–200) |

**Lead time:** Nairobi: same day. AliExpress: 2–4 weeks.

## Wiring Diagram

```
  Kenya Mains (240V AC)
  ┌─────────────────────────────────┐
  │  ⚠ DANGER: 240V MAINS ⚠        │
  │  Must be wired by a qualified   │
  │  electrician. Use a circuit     │
  │  breaker and RCD/GFCI.         │
  └──────────┬──────────────────────┘
             │
             ▼
  ┌─────────────────────┐
  │  12V/2A Power Supply │
  │  (AC→DC converter)   │
  │                       │
  │  AC IN: L, N, Earth   │
  │  DC OUT: +12V, GND    │
  └───┬──────────┬────────┘
      │          │
   +12V        GND
      │          │
      ├──────────┼──── To safety beam TX (+12V, GND)
      │          │
      ├──────────┼──── To safety beam RX (+12V, GND)
      │          │
      ├──────────┼──── To 12V-to-5V buck converter INPUT
      │          │
      │          │
      ▼          ▼
  ┌─────────────────────┐
  │  5V Buck Converter    │
  │  (12V→5V step-down)   │
  │                       │
  │  IN:  +12V, GND       │
  │  OUT: +5V,  GND       │
  └───┬──────────┬────────┘
      │          │
     +5V        GND
      │          │
      ├──────────┼──── To ESP32-S3 (5V pin and GND)
      │          │
      ├──────────┼──── To relay module (VCC and GND)
      │          │
      └──────────┘

  IMPORTANT: All GND connections are common (tied together).
```

## Step-by-Step Assembly

### What You Need

- [ ] 12V/2A AC-DC power supply
- [ ] LM2596 5V DC-DC buck converter module
- [ ] Wire: 18 AWG (1.0mm²) for 12V lines, 22 AWG (0.5mm²) for 5V lines
- [ ] Wire strippers
- [ ] Small flathead screwdriver
- [ ] Multimeter (essential for verifying voltages before connecting anything)

### Steps

**Step 1 — Verify the 12V power supply before connecting anything else.**
Connect the 12V PSU to mains power (have an electrician do this if you're not comfortable with 240V). Use a multimeter set to DC voltage. Touch the red probe to the +12V terminal and the black probe to the GND terminal. The display should read between 11.8V and 12.3V. If it reads 0V, check the mains connection. If it reads a negative number, you have the probes reversed.

**Step 2 — Set up the buck converter.**
The LM2596 module has 4 screw terminals: IN+ IN- OUT+ OUT-. It also has a small blue potentiometer (a tiny screw you turn with a screwdriver).
- Connect IN+ to the 12V PSU's +12V terminal.
- Connect IN- to the 12V PSU's GND terminal.
- **BEFORE connecting anything to OUT:** Turn the potentiometer counterclockwise about 10 turns (so it starts at a low voltage).
- Measure the voltage between OUT+ and OUT- with your multimeter.
- Slowly turn the potentiometer **clockwise** until the multimeter reads exactly **5.0V** (anywhere between 4.9V and 5.1V is fine).
- Once set, the output will stay at 5V. You should not need to adjust it again.

**Step 3 — Connect 5V to the ESP32-S3.**
Run a wire from the buck converter's OUT+ to the ESP32-S3's **5V** pin. Run another wire from OUT- to the ESP32-S3's **GND** pin. **Remove the USB cable first** — you don't want to power the ESP32 from both USB and the supply simultaneously.

**Step 4 — Connect 5V to the relay module.**
Run a wire from the buck converter's OUT+ to the relay module's **VCC**. Run another wire from OUT- to the relay module's **GND**.

**Step 5 — Connect 12V to the safety beam sensors.**
Run wires from the 12V PSU's +12V and GND terminals to the safety beam TX and RX units.

**Step 6 — Final check.**
With the multimeter, verify:
- 12V PSU output: 11.8–12.3V
- Buck converter output: 4.9–5.1V
- ESP32-S3 powers on (red LED)
- Relay module power LED is on (green)
- Safety beam sensors are powered (indicator LEDs)

## First Power-On

**What you should see:**
1. 12V PSU: a small green LED on the PSU enclosure (if equipped).
2. Buck converter: a small red LED on the module (indicates output is active).
3. ESP32-S3: red power LED.
4. Relay module: green power LED.
5. Safety beam: indicator LED on the RX unit.

**If the ESP32 doesn't power on:**
- Check the buck converter output with a multimeter. If it reads 0V, the potentiometer may need adjustment.
- Check that the polarity is correct (5V to the 5V pin, GND to GND — never reversed).

## Failure Signatures

| Symptom | Cause | Fix |
|---------|-------|-----|
| 12V PSU has no output | Mains not connected or fuse blown | Check mains cable. Check PSU's internal fuse (if accessible). |
| Buck converter outputs wrong voltage | Potentiometer not adjusted | Turn potentiometer with multimeter attached until reading is 5.0V. |
| ESP32 resets randomly under load | 5V supply can't handle relay + ESP32 | Use a beefier buck converter (5A instead of 3A), or power relays from a separate 5V supply. |
| Relay module clicks cause ESP32 brown-out | Current spike when relay switches | Add a 1000µF electrolytic capacitor across the 5V rail near the ESP32. |
| Components get hot | Short circuit or overload | Immediately disconnect power. Check all wiring for shorts. |

## Safety Notes

- **240V MAINS IS LETHAL.** All connections to mains voltage (the AC input side of the 12V PSU) must be done by a qualified electrician. Use a circuit breaker and RCD (Residual Current Device / GFCI) on the mains circuit.
- **Never touch the AC terminals** of the 12V PSU while it is plugged in. Even after unplugging, capacitors inside may hold a charge for several seconds.
- **Always verify voltages with a multimeter** before connecting any component. A buck converter set to the wrong voltage (e.g., 12V output instead of 5V) will instantly destroy the ESP32.
- **Use fuses:** Place a 3A fuse on the 12V line and a 2A fuse on the 5V line for overcurrent protection.
- **For production:** Use a proper DIN-rail power supply with CE/UL certification, inside a sealed IP65 enclosure. The prototype bench setup is not suitable for outdoor deployment.
