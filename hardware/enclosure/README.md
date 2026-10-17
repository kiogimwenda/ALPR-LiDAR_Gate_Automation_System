# Field controller enclosure fit

How the gate field controller PCB (`hardware/pcb/gate-field-controller/`) mounts
in the field enclosure. **If the board outline, a mounting hole or an edge
connector moves, update this file and `board-mounting-template.dxf` in the
same change.**

## Enclosure

IP65 ABS junction box, 200 × 150 × 100 mm external (BOM item 11,
`hardware/bom/prototype-bom.md`). Internal dimensions vary by vendor, so
measure the actual box before you drill. The box also holds the 12 V PSU and
the PoE switch (`docs/hardware/07-power-supply.md`, `08-network-switch.md`).

## Board

| Item | Value |
|------|-------|
| Outline | **100 × 90 mm**, 3 mm corner radius, 1.6 mm FR-4 |
| Mounting holes | 4× M3 (Ø3.2 mm plated, pad tied to GND) |
| Hole centres (top view, from top-left corner) | (4, 4), (96, 4), (4, 86), (96, 86): a **92 × 82 mm** pattern |
| Standoffs | M3, **≥ 6 mm**. Through-hole leads stick out up to 3.5 mm below the board |
| Tallest part above the board | SRD relays, 15.6 mm (KiCad 3D model). Screw terminals 13.8 mm, 100 µF electrolytic 10.5 mm, pluggable headers 7.5 mm before the plug is fitted |
| Stack height from the back plate | 6 + 1.6 + 15.6 ≈ **23 mm**. Leave ≥ 30 mm for wiring and plugs. The 100 mm-deep box has ample room |

The HR911105A RJ45 has no KiCad 3D model. Check its height against the HanRun
drawing before you set the lid clearance.

`board-mounting-template.dxf` is a 1:1 top-view drilling template. Its origin
is the board's bottom-left corner, Y up, in mm. It has these layers:
`BOARD_OUTLINE`, `DRILL_M3` (Ø3.2), `STANDOFF_6MM` (standoff footprint),
`CENTRE` and `ANTENNA_KEEPOUT_NO_METAL`. Print it at 100 % to mark the back
plate.

## Edge connectors

Positions are measured from the board's top-left corner in the top view
(x to the right, y down). The cable side of each connector faces outward
from the board edge. The screw terminals' (J9, J10) bodies sit 0.4 mm inside the
board outline, the pluggable headers (J1, J7, J8) are flush with it, and every pin's function is printed on the silkscreen.

| Edge | Connector | Position | Notes |
|------|-----------|----------|-------|
| Left | J1 12 V IN (MC 1,5 3.81, 2-pos pluggable) | y 35.5 (+), 39.3 (−) | Main supply from the PSU. Plug MC 1,5/2-ST-3,81 mates from the left; leave ≥ 30 mm clear for plug and wire bend |
| Top | J3 USB-C | x 18 | Service and programming port only. It needs no box cutout: open the lid to flash |
| Top | J5 JTAG 1×5 header | x 78–88 | Service only |
| Right | J6 RJ45 (HR911105A) | footprint y 8.1–27.2 (jack centre y ≈ 17.6) | Cat5e/6 to the PoE switch. Leave cable bend room to the right wall |
| Right | J10 RS-485 (MKDS 5.08, 3-pos) | GND y 47.0, B y 52.1, A y 57.2 | Wire entries face the right wall |
| Right | J9 BEAM 12 V (MKDS 5.08, 2-pos) | − y 72.1, + y 77.2 | Fused (F2) supply for the safety beam. Wire entries face the right wall |
| Bottom | J7 RELAY OUT (MC 1,5 3.81, 12-pos pluggable) | pins x 11.5–53.4 | Plug mates from below. Leave ≥ 30 mm clear below the board edge for plug and wire bend |
| Bottom | J8 SENSE IN (MC 1,5 3.81, 6-pos pluggable) | pins x 61.0–80.1 | As for J7 |

Field cables enter through glands. Put them in the wall nearest the
connector they serve: bottom edge for relays and sensors, right edge for
Ethernet, RS-485 and the beam.

## Antenna (ESP32-S3-WROOM-1)

The module's PCB antenna sits on the **top edge, x 28–76 mm**. Keep metal
out of that zone and out of the space beyond the board edge in front of it.
That means no metal back plate under it, no PSU or switch housing, and no
cable bundle across it. Orient the board so the top edge faces a plastic
wall or the lid, away from the PSU and the switch. A metal mounting plate
needs a cut-out matching `ANTENNA_KEEPOUT_NO_METAL`.

## Indicators (for a translucent lid or light pipes)

| LED | Position (x, y) |
|-----|-----------------|
| PWR (D5) | 40.3, 18.9 |
| Status RED / GREEN / YELLOW / NET (D61–D64) | 60.0 / 63.9 / 67.7 / 71.5, all at y 45.3 |
| Relay 1–4 on (D15–D18) | 8.5 / 26.0 / 43.5 / 61.0, all at y 48.6 |

The buttons sit together beside the USB-C port in the top-left programming corner:
BOOT (SW2) at 6.5, 21.0 and RESET (SW1) at 17.0, 21.0. To flash, plug in USB, hold
BOOT and tap RESET. Both are service-only, so you reach them with the lid open.
