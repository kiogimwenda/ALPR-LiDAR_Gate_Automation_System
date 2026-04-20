# Component Guide 05: 4-Channel 5V Relay Module — Gate Motor Control

## What This Part Does

This board has 4 electrical switches (relays) that the ESP32-S3 can turn on and off. When a relay closes, it connects two wires — just like flipping a light switch. We use two of these relays to send "trigger" pulses to the gate motor controller, telling it to open or close the gate. The other two are spares for future use (e.g., a buzzer, a light, or a second gate motor).

## Exact Part

- **Type:** 4-Channel 5V Relay Module with Optocoupler Isolation
- **Relay rating:** 10A @ 250V AC or 10A @ 30V DC per channel
- **Control voltage:** 5V (relay coil), 3.3V–5V trigger (works directly with ESP32-S3's 3.3V GPIOs)
- **Trigger:** Active LOW (connect to GND to activate) — most modules support both HIGH and LOW trigger via a jumper
- **Isolation:** Optocoupler between control side and relay side

**Important:** Get a module with **optocoupler isolation** (the listing will say "with optocoupler" and the board will have small black 4-pin chips near the relay coils). This protects the ESP32 from electrical noise when the relays switch.

## Where to Buy

| Source | Approximate Price | Notes |
|--------|-------------------|-------|
| **AliExpress** — search "4 channel 5V relay module optocoupler" | USD 2–4 (~KES 260–520) | Extremely common part. Any seller with 95%+ rating. |
| **Luthuli Avenue, Nairobi** — Nuvision, Kemu, Redflag Electronics | KES 300–500 | Ask for "4-channel relay module, the one with optocoupler." |
| **Amazon** — search "4 channel relay module 5V optocoupler" | USD 4–8 | HiLetgo, DIYables, or Oiyagai brands. |

**Lead time:** AliExpress 2–4 weeks. Nairobi: same day.

## What's in the Box

- 1× 4-channel relay module board
- 4× Screw terminals already mounted (for connecting wires to each relay's output)
- Pin headers pre-soldered on the control side

### What You Need to Buy Separately

- 4× jumper wires (female-to-male for breadboard connection)
- Wire for the relay output side (18 AWG / 1.0mm² stranded wire, any color — about 1m)
- Small flathead screwdriver (for the screw terminals — 2mm blade)

## Pinout and Wiring

```
  Relay Module (control side)        ESP32-S3
  ┌──────────────────────────┐
  │  VCC ────────────────────┼─── 5V (pin 20 on ESP32-S3)
  │  GND ────────────────────┼─── GND
  │  IN1 ────────────────────┼─── IO4 (Relay 1 — gate leaf 1)
  │  IN2 ────────────────────┼─── IO5 (Relay 2 — gate leaf 2 / spare)
  │  IN3 ────────────────────┼─── (not connected — future use)
  │  IN4 ────────────────────┼─── (not connected — future use)
  └──────────────────────────┘

  Relay Module (output side — screw terminals)
  Each relay has 3 terminals:
  ┌─────────┐
  │ COM ────┼─── Common (center terminal)
  │ NO  ────┼─── Normally Open (disconnected when relay is OFF)
  │ NC  ────┼─── Normally Closed (connected when relay is OFF)
  └─────────┘

  For gate motor trigger, use COM and NO:
  - When ESP32 activates the relay → COM and NO connect → gate motor sees a "button press"
  - When ESP32 deactivates the relay → COM and NO disconnect → motor trigger released

  Wiring to gate motor (for a sliding gate, e.g., CENTURION D5-Evo):
  ┌──────────────────┐        ┌───────────────────────────────┐
  │  Relay 1         │        │  Gate Motor Controller        │
  │  COM ────────────┼────────┤  TRIGGER terminal             │
  │  NO  ────────────┼────────┤  TRIGGER COMMON / GND         │
  └──────────────────┘        │                               │
                              │  (This simulates pressing     │
                              │   the gate remote button)     │
                              └───────────────────────────────┘

  For a dual-leaf swing gate:
  Relay 1 COM/NO → Motor Controller LEAF 1 TRIGGER
  Relay 2 COM/NO → Motor Controller LEAF 2 TRIGGER
```

## Step-by-Step Assembly

### What You Need

- [ ] 4-channel relay module
- [ ] ESP32-S3 on breadboard (from Guide 01)
- [ ] 4× jumper wires
- [ ] Small flathead screwdriver

### Steps

**Step 1 — Place the relay module.**
Set the relay module on the desk next to the breadboard. Do NOT put it on the breadboard — it's too large and the relay coils create electrical noise. Keep it separate, connected only by wires.

**Step 2 — Connect power.**
- Red wire from relay module **VCC** → ESP32-S3 **5V** pin (pin 20).
- Black wire from relay module **GND** → ESP32-S3 **GND** pin.

**Why 5V for VCC:** The relay coils need 5V to switch. The optocoupler on the control side works with 3.3V trigger signals from the ESP32.

**Step 3 — Connect control signals.**
- Wire from relay module **IN1** → ESP32-S3 **IO4**
- Wire from relay module **IN2** → ESP32-S3 **IO5**

**Step 4 — Test with a simple program.**
Before connecting to the gate motor, test that the relays click. Upload this program to the ESP32:

```cpp
#include <cstdio>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
    printf("Relay Module Test\n");

    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT);

    // Relays are active LOW on most modules
    gpio_set_level(GPIO_NUM_4, 1);  // relay 1 OFF
    gpio_set_level(GPIO_NUM_5, 1);  // relay 2 OFF

    while (true) {
        printf("Relay 1 ON (click!)\n");
        gpio_set_level(GPIO_NUM_4, 0);  // relay 1 ON
        vTaskDelay(pdMS_TO_TICKS(1000));

        printf("Relay 1 OFF\n");
        gpio_set_level(GPIO_NUM_4, 1);  // relay 1 OFF
        vTaskDelay(pdMS_TO_TICKS(1000));

        printf("Relay 2 ON (click!)\n");
        gpio_set_level(GPIO_NUM_5, 0);  // relay 2 ON
        vTaskDelay(pdMS_TO_TICKS(1000));

        printf("Relay 2 OFF\n");
        gpio_set_level(GPIO_NUM_5, 1);  // relay 2 OFF
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**What you should hear:** A distinct "click" sound from each relay as it switches on and off. The relay's red LED indicator should light up when it's ON.

**Step 5 — Connect to the gate motor (ONLY when everything else works).**
- Use 18 AWG stranded wire. Strip 8mm of insulation from each end.
- Loosen the screw terminal on relay 1's **COM** terminal. Insert the wire. Tighten the screw firmly (not overtight — just snug).
- Do the same for relay 1's **NO** terminal.
- Run these two wires to the gate motor controller's TRIGGER terminals (see your motor's manual for which terminals).

## First Power-On

**What you should see:**
1. The relay module's power LED (green) should light up when VCC and GND are connected.
2. No relay LEDs should be on (all relays OFF by default).
3. When your test program runs, you should hear clicks and see the red indicator LEDs toggle.

## Failure Signatures

| Symptom | Cause | Fix |
|---------|-------|-----|
| No power LED on relay module | VCC not connected or wrong voltage | Check wire from relay VCC to ESP32 5V pin. |
| Relay doesn't click | Wrong trigger polarity | Try changing `gpio_set_level(pin, 0)` to `1` or vice versa. Check the jumper for HIGH/LOW trigger. |
| Relay clicks but gate doesn't move | Wrong motor terminals or wiring | Check gate motor manual for TRIGGER terminal locations. Test with a multimeter — when relay clicks, COM-NO should show 0Ω (continuity). |
| ESP32 resets when relay clicks | Power brownout from relay coil | Add a 470µF capacitor between 5V and GND near the relay module. Or power the relay from a separate 5V supply. |
| Gate opens/closes but won't stop | Relay stuck ON, or pulse too long | Ensure the relay pulse is short (200ms). The gate motor controller expects a momentary trigger, not a held signal. |

## Datasheet Links

- No specific manufacturer datasheet — this is a generic module. The relay chip is typically a Songle SRD-05VDC-SL-C.
- [Songle SRD-05VDC-SL-C Datasheet](https://www.circuitbasics.com/wp-content/uploads/2015/11/SRD-05VDC-SL-C-Datasheet.pdf)

## Safety Notes

- **The relay output side can switch mains voltage (250V AC).** In this project, we are NOT switching mains — we are only switching the gate motor's low-voltage trigger signal. However, **never** connect mains wiring to these screw terminals unless you are a licensed electrician.
- **Gate motors have pinch and crush hazards.** Always test with the gate disconnected from any people traffic area first. Never put your hand in the gate track while the motor is powered.
- **The relay coils create electromagnetic noise** that can cause the ESP32 to glitch. Using the optocoupler-isolated module and keeping relay wires away from SPI/UART signal wires reduces this.
- **For production:** replace the relay module with a proper industrial relay with DIN rail mounting and proper terminal blocks. The breadboard-style module is for prototyping only.
