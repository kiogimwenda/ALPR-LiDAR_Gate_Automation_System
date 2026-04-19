# ADR-008: Gate Actuator Interface Strategy

## Status
Accepted

## Date
2026-04-19

## Context

The system must control **sliding gates** and **dual-leaf swing gates** commonly found in Kenyan residential estates. Popular gate motor brands in Kenya include CENTURION (South African, dominant market share), ET Systems, CAME, and BFT. The PCB must interface with these motors to command open/close and read limit-switch status.

### Gate Motor Interface Methods

Most residential gate motors expose one or more of these control interfaces:

1. **Dry-contact relay (trigger input)** — The motor controller has a "trigger" terminal. Shorting it briefly (pulse) toggles the gate: stopped → open, opening → stop, open → close. This is the universal interface — every motor has it. The PCB drives a relay or optocoupler to simulate the pulse.

2. **RS-485 / serial** — Some CENTURION motors (D5-Evo, D10) support RS-485 for integration with access control systems. Allows querying status (position, fault codes) and issuing discrete commands (OPEN, CLOSE, STOP). More capable but proprietary protocol.

3. **Proprietary bus** — CAME, BFT, and others have proprietary protocols. Reverse engineering is possible but fragile across firmware versions.

### Options Evaluated

1. **Dry-contact relay only**
   - Universal — works with any gate motor brand
   - Simple: one GPIO → relay → motor trigger terminal
   - Limited feedback: no position data, rely on limit switches
   - Requires separate limit-switch wiring for open/closed detection

2. **RS-485 primary, relay fallback**
   - Richer data from motor controller (position, current draw, fault codes)
   - Only works with motors that support RS-485 (CENTURION D5-Evo, D10)
   - Relay fallback for motors without RS-485

3. **Direct motor control (H-bridge)**
   - Full control of motor direction and speed
   - Bypasses the motor controller entirely
   - Dangerous: loses built-in safety features (soft start/stop, current limiting, anti-crush)
   - Warranty void

### Ranking

**Option 1 (Dry-contact relay) is best for initial deployment** because:
- Works with every gate motor brand found in Kenyan estates
- Simplest wiring — two wires from PCB relay to motor trigger terminal
- Safety features remain in the motor controller (anti-crush, soft start)
- Separate limit switches (magnetic reed or microswitch) provide open/closed feedback

**Option 2 (RS-485 + relay fallback) is the future enhancement** once the system is proven with relay-only. RS-485 support would unlock position feedback and fault monitoring for CENTURION motors.

## Decision

Use **dry-contact relay** as the primary gate actuator interface. The PCB has:
- 2× relay outputs (for dual-leaf: one relay per leaf; for sliding: one relay for trigger, one spare for second function like pedestrian mode)
- 2× limit-switch inputs (gate fully open, gate fully closed) via reed switches or microswitches
- 2× safety sensor inputs (photoelectric beam across gate opening for obstruction detection)
- RS-485 transceiver on the PCB (MAX485 or similar) for future CENTURION integration, active only via config flag

### Wiring for Sliding Gate (e.g., CENTURION D5-Evo)
- Relay 1 → TRIGGER terminal (pulse to toggle)
- Limit switch 1 → OPEN position
- Limit switch 2 → CLOSED position
- Safety beam → across gate opening

### Wiring for Dual-Leaf Swing Gate (e.g., CENTURION R5)
- Relay 1 → TRIGGER terminal leaf 1
- Relay 2 → TRIGGER terminal leaf 2
- Limit switch 1 → both leaves fully open
- Limit switch 2 → both leaves fully closed
- Safety beam → across gate opening

## Consequences

- State machine uses pulse-based control: short relay pulse (200ms) to toggle motor
- Open/closed detection via limit switches, not motor feedback
- Timeout-based fault detection: if gate doesn't reach limit switch within expected time → FAULT state
- Gate type (sliding vs dual-leaf) is a runtime configuration parameter
- RS-485 driver included in PCB design but disabled in firmware until protocol is implemented
