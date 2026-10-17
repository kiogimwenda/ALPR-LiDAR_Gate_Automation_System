# Routing pipeline for gate-field-controller

These scripts produced the routed board in `../gate-field-controller/`. They run
under WSL against the Windows KiCad 10 install (`python.exe` with `pcbnew`,
`kicad-cli.exe`). Freerouting 2.5.0 needs Java 25; the Windows JDK works.

| Step | Script | What it does |
|------|--------|--------------|
| 0 | `relabel.py` | Run once on the unrouted placement. Turns the J1/J9/J10 screw terminals so their wire entries face out of the board edge, groups RESET/BOOT beside the USB-C port, and writes the full silkscreen legend (connector names, every pin's function, test-point voltages, status LEDs). |
| 0b | `swap_j1.py` | Rev 2.2: replaces the J1 screw terminal with a Phoenix MC 1,5/2-G-3.81 pluggable header (plug enters from the left edge) and bumps the back-side revision text. |
| 1 | `prep.py` | Re-places the crystal group, D3 and the W5500 top caps. Hand-routes the parts the autorouter handled badly: the crystal network (XI/XO/R21/R22/C27/C28, top layer, no vias), and the W5500 west-side pins escaping inward to vias under the IC body. Also hand-routes the AVDD bus inside the body plus its exit to C23/C24/FB1, and the top-edge pins to C25/C26. The buck switch node becomes a solid F.Cu pour. All hand copper is locked. |
| 2 | `fanout.py` | Gives every remaining GND/+3V3 SMD pad a short stub and a clearance-checked via to its inner plane. |
| 3 | `dsn.py` + Freerouting | Exports Specctra DSN (inner layers marked `power`), then autoroutes everything else with the project's net classes. |
| 4 | `post.py` | Imports the session and removes dangling copper. Widens Freerouting's 0.15 mm neck-downs to the 0.2 mm spec. Snaps off-centre fine-pitch pad exits onto the pad centreline, and nudges plane vias that ended up too close to autorouted tracks. |
| 5 | `finish.py` | Adds GND fills on F.Cu/B.Cu (SMD solid, THT thermal, no floating islands) and solid connections for the ESP32/TPS5430 thermal pads and the RJ45 shield. Adds GND stitching vias on a 5 mm grid that respect the relay-contact 0.6 mm clearance and the antenna keep-out. |
| — | `maze.py` | Two-layer A* router for a single leftover connection. Diagnostic and repair tool, not part of the normal run. |
| — | `geo.py` | Shared clearance geometry. |

`all.sh <tag>` runs steps 1–5 and DRC. It expects these files beside the scripts:
- `route/placed.kicad_pcb`: the unrouted placement, i.e. `../gate-field-controller/gate-field-controller.placed-unrouted.kicad_pcb`.
- `route/gate-field-controller.kicad_pro` and the `*.kicad_sch` files.
- `fr/freerouting-2.5.0.jar`.

Freerouting is randomised, so run a few tags in parallel and keep one whose DRC is clean. Every intermediate board needs a `<stem>.kicad_pro` copy beside it: KiCad loads net classes and rules from the project file whose name matches the board. Without it, the board silently falls back to default rules.
