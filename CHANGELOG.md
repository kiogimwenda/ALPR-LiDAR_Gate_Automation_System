# Changelog

All notable changes to the ALPR + LiDAR Automated Vehicle Gate Control
System. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions are tagged `vX.Y.Z-phaseN` matching the
[development timeline](README.md#development-timeline). The full build
story — every sub-milestone with design reasoning — lives in the
README's chronological log; this file is the executive summary.

## [Unreleased]

- PCB design guide revision 3.11: caught while sourcing the J_RELAY
  connector — the guide's "3.81mm signal connector" spec (J_RELAY,
  J_SENSE) pointed at Phoenix's MKDS-1,5 series, which only exists
  at 5.08mm/5.00mm pitch; there's no 3.81mm MKDS-1,5 part, so
  neither footprint ever matched anything real. The actual 3.81mm
  parts are Phoenix's separate MC series, in a different KiCad
  footprint library (`Connector_Phoenix_MC`, not
  `TerminalBlock_Phoenix`). Corrected both, added the missing
  `Connector:Screw_Terminal_01xNN` symbol name to all four screw
  terminals (previously only J_PWR mentioned one), and corrected
  J_PWR/J_RS485's 5.08mm footprint names to match the library's
  real naming. BOM's LCSC numbers for J4/J5 were tied to the old
  nonexistent description and are left unpinned pending
  re-verification against the corrected parts.
- PCB design guide revision 3.10: §4.4.1's component table has
  listed `LED_REL` and its `R_LED` (1kΩ, calculated off the 5V rail)
  since early revisions, but neither one ever appeared in the
  schematic diagram or the pin-to-pin table — nowhere in the
  document said where to actually connect it. Added: LED_REL sits
  in parallel with the relay coil at the transistor's collector,
  anode through R_LED to +5V, cathode to the collector node — lights
  when the transistor actually conducts (relay energized), not just
  when the GPIO commands it, which is also why R_LED was always
  calculated off 5V rather than 3.3V.
- PCB design guide revision 3.9: §4.4's KiCad symbol name was wrong
  - `Relay:SANYOU_SRD_Series_Form_C` doesn't exist, the symbol is
  `SANYOU_SRD_Form_C` (the matching footprint does keep "Series" in
  its own name, which is what made this easy to get wrong). This
  exact symbol also has a documented KiCad bug history
  (kicad-footprints#1003: schematic/footprint pin mismatch, fixed in
  kicad-symbols#1527). Added a verified pin table (1, 2 = coil, no
  polarity; 5 = COM; 4 = NO; 3 = NC) and real pin numbers throughout
  the schematic and pin-to-pin table, which previously only referred
  to contacts generically as "COM/NO/NC" with no pin numbers.
- PCB design guide revision 3.8: §4.3's crystal network was a single
  sentence of prose with no visual layout, which turned out not to
  be enough to wire the network correctly. Replaced with an actual
  diagram: R16 (1MΩ) bridges XI and XO directly, not in series with
  either crystal leg; the crystal's two terminals go one each to
  XI/XO, each with its own 18pF load cap to GND; R17 (populated as a
  0Ω link per WIZnet's reference) sits in series on the XI-side leg,
  though at 0Ω its exact side is electrically immaterial.
- PCB design guide revision 3.7: §4.3's RJ45 diagram was a black box
  with no real pin numbers, caught while wiring the actual W5500
  sheet. Replaced with HanRun's actual HR911105A internal schematic:
  TD+/TD-/RD+/RD- mapped to the W5500's TXP/TXN/RXP/RXN, TCT/RCT
  confirmed unconnected (no external bias per HanRun's own
  reference), CHS GND wired to PCB ground per the datasheet (was
  entirely undocumented before). Also fixed: the built-in Green/
  Yellow LEDs were wired backwards — W5500's LINKLED/SPDLED/DUPLED/
  ACTLED are active-low, not active-high, so anode goes to +3.3V and
  the W5500 pin sits on the cathode side. Section 4.7's LED_NET has
  the same fix, since it's driven by the same LINKLED signal.
  R_LED count for the RJ45's LEDs corrected from x4 to x2 (the part
  only has two LED elements); BOM's 330Ω resistor count updated
  8 -> 9 to match.
- PCB design guide revision 3.6: §4.3's RJ45 row named the part as
  HR911105A (Hanrun) but pointed the footprint at the Amphenol
  ARJM11C7 footprint instead — different manufacturer, different pad
  layout. Corrected to `RJ45_Hanrun_HR911105A_Horizontal` for both
  symbol and footprint (also the only Hanrun-branded RJ45 symbol
  current KiCad libraries actually ship; the `RJ45_Shielded_Magjack`
  name previously cited doesn't exist).
- PCB design guide revision 3.5: rev 3.4's USB-C diagram split the
  connector's own pins and the U6 ESD IC's pinout into two separate
  blocks, and that split turned out ambiguous enough that the next
  schematic built from it wired U6 downstream of the 22Ω series
  resistors (same net as the ESP32) instead of upstream, right at
  the connector — which defeats much of the point of the clamp,
  since the resistor then sits in the fast-transient path before
  the clamp engages. Replaced with one linear diagram stating the
  order explicitly: connector → U6 → resistor → ESP32.
- PCB design guide revision 3.4: §4.2.4's USB-C diagram showed a
  single "D+"/"D-" line each, which doesn't match the
  `USB_C_Receptacle_USB2.0_16P` symbol's real pins — D+ is two
  separate visible pins (A6, B6) and D- is two separate visible pins
  (A7, B7), one per plug orientation, neither hidden/auto-merged
  (unlike VBUS/GND on the same symbol, which do have hidden
  duplicates). Diagram now shows both physical pins per signal
  needing to be tied together, with a single 22Ω resistor on the
  combined net.
- PCB design guide revision 3.3: §4.2.1's KiCad symbol name for the
  USB-C receptacle was stale — `Connector:USB_C_Receptacle_USB2.0`
  doesn't exist in current KiCad 10 libraries (split into `_14P`/
  `_16P` variants at some point). Corrected to
  `USB_C_Receptacle_USB2.0_16P`, matching the GCT USB4105-GF-A's
  actual 16 contacts (`_14P` omits the SBU1/SBU2 pins this part has).
- PCB design guide revision 3.2: caught reviewing the first built
  USB-C/MCU sheet — the USB-C ESD IC (USBLC6-2SC6) was miswired
  (pin 5, which is VBUS, tied into the shield/GND chain instead,
  with the D+/D- clamp pins left unconnected), traced back to the
  guide's own diagram only describing it as a black box with no real
  pin numbers; now gives the IC's actual pinout per ST's datasheet.
  Also clarified CC1/CC2 need independent 5.1kΩ pull-downs, never a
  shared one. Added §4.2.5 "Programming & Debug Header Wiring" —
  J_PROG and J_DBG had footprints since rev 2.0 but no documented
  pinout anywhere; now: GND/3V3/TXD0/RXD0 for J_PROG, and the four
  otherwise-unused JTAG GPIOs (IO39-42, with the eFuse caveat) for
  J_DBG.
- PCB design guide revision 3.1: every capacitor in §4 now states its
  type in plain words — ceramic/unpolarized (X7R/X5R/C0G) or
  electrolytic/polarized — instead of relying on a dielectric code
  or footprint name alone; several rows previously gave a value with
  no dielectric at all (C_boot, C2, C_TOCAP, C_1V2O, C_filter, C_dec,
  C_shield). Also fixed two small rev-3.0 gaps found in the same
  pass: Appendix B's crystal-load-cap BOM row still said 20pF instead
  of the corrected 18pF, and C_1V2O never got a BOM line.
- PCB design guide revision 3.0: restructured chapters 4 (footprints)
  and 5 (schematics) into a single chapter with one section per
  module — footprints, schematic diagram, and an explicit
  pin-to-pin connection table together, instead of split across two
  chapters. Screw terminals/connectors moved into the module section
  they belong to. Re-deriving the pin-to-pin tables against the
  WIZnet W5500 datasheet directly turned up real, pre-existing
  errors: the SPI/INT pin numbers in Appendix A were off by several
  positions (correct: SCLK=33, MOSI=35, MISO=34, SCSn=32, INTn=36);
  the 12.4kΩ bias resistor was drawn on a `RSVD` pin instead of
  `EXRES1` (pin 10), and pin 23 (also `RSVD`) actually needs a
  direct tie to GND; "1V8OUT" doesn't exist on this chip — it's
  `1V2O`, needing 10nF, not 10µF; and the crystal circuit was
  missing WIZnet's own 1MΩ feedback resistor entirely. Also added:
  a corrected ESP32 decoupling explanation (schematic vs. layout
  step) and coverage of the module's hidden GND pins (40/41) and
  why they need a plain `GND` net to auto-connect to. Later chapters
  renumbered down by one to close the gap.
- PCB design guide revision 2.1: fixed a wrong 5V-rail feedback
  divider (43kΩ/10kΩ actually set ~6.47V, not the documented 5.0V;
  corrected to 30.9kΩ/10kΩ), added the reverse-polarity and buck
  catch/freewheeling diodes to §5.1's power-path diagram and the BOM
  (previously only in the §4.3 component table, never drawn — easy to
  miss), and removed an erroneous compensation-network entry for a
  chip (TPS5430) that has no COMP pin. Caught during review of the
  first built power-supply schematic sheet.
- PCB design guide rewritten as revision 2.0 for **KiCad 10.0**
  (`docs/hardware/10-pcb-design-kicad10.{html,pdf}`), with the pin map
  **re-synchronized to firmware v1.0.0** — rev 1.0 predated the
  firmware drivers and disagreed with them on the entire W5500 SPI
  bus, INT/RST, the status LEDs, and RS-485 placement. §1.4 is now
  the routing source of truth.
- Hardware prototyping and bench validation
  ([plan](docs/hardware/11-bench-validation-plan.md)), then site
  commissioning ([checklist](docs/commissioning.md)).

## [v1.0.0-phase6] — 2026-07-19

**Delivery.** The software stack is complete, demonstrable, and
operable by someone who didn't build it.

### Added
- Operations runbook ([docs/runbook.md](docs/runbook.md)) — day-2
  operations: health checks, allowlist administration, credential
  rotation, OTA releases, backup/restore, incident playbooks.
- Site commissioning checklist
  ([docs/commissioning.md](docs/commissioning.md)) — bench-validated
  hardware → live residential gate, with acceptance tests and sign-off.
- One-command demo (`scripts/demo-stack.sh` +
  [docs/demo.md](docs/demo.md)) — the full loop on a laptop, no GPU:
  scripted detections denied/authorized, gates physically traveling,
  optional `--hardened` mTLS + JWT posture.
- This changelog, CONTRIBUTING.md, and retroactive phase tags.

## [v0.6.0-phase5] — 2026-07-18

**Vision ingest & hardware readiness.** The product's core loop closes:
a detection opens the gate with no human in the loop.

### Added
- ADR-012: camera–LiDAR fusion unit adopted for production sensing
  (commercial tier); ADR-006/007 retained for prototype/pilot and
  residential production.
- `server/vision/`: sensor-agnostic `ObservationSource` seam,
  fail-closed JSON scenario source, `DetectionFrame` builder, and a
  `SubmitDetection` client with the site TLS ladder.
- `gate-vision` daemon (systemd unit + installer wiring) and a
  GPU-gated `CameraSource` (RTSP → TensorRT ALPR, compile-verified).
- Auto-dispatch: an AUTHORIZED verdict publishes `OPEN_GATE` to the
  detected gate's Control stream, audit-tied to its decision id;
  denials never dispatch.
- `e2e_vision_stack` + vision pass inside `e2e_hardened_stack`;
  bench validation plan
  ([docs/hardware/11-bench-validation-plan.md](docs/hardware/11-bench-validation-plan.md)).

### Test suite
164 tests (CPU) / 170 (GPU), including five multi-process E2E passes.

## [v0.5.0-phase4] — 2026-07-11

**Implementation.** Everything between the wire contract and delivery:

### Added
- TensorRT inference: `TrtEngine` RAII wrapper, YOLOv9 plate detector,
  PaddleOCR recognizer, ALPR pipeline, ONNX→plan tooling (4.2).
- Decision server: SQLite allowlist/blocklist store, fusion verdict
  ladder, event broadcaster, gRPC services, `gate-server` daemon (4.3).
- ESP32-S3 firmware: W5500/relay/limit/beam/LED drivers, pure-C++20
  state machine (host-tested), nanopb + nghttp2 h2c gRPC client
  (ADR-011), self-hosted signed OTA (ADR-009) (4.4–4.5).
- Simulation harness (`gate-sim`, `gate-mock-server`) (4.6).
- Dashboard: Drogon backend (REST + WebSocket) + SvelteKit frontend (4.7).
- Deployment: systemd units, installer, nginx OTA site (4.8).
- Multi-process E2E harness (4.9).
- Security hardening: site PKI + mTLS everywhere, JWT admin auth,
  firmware esp-tls + HTTPS OTA, [docs/security.md](docs/security.md)
  (4.10).

## [v0.4.1-proto] — 2026-04-25

### Added
- Phase 4.1: the `gate.v1` gRPC wire contract
  (`shared/proto/gate_service.proto`) with descriptor-validation tests.

## [v0.2.0-phase3] — 2026-04-20

### Added
- Phases 1–3: design clarifications; architecture (7 Mermaid
  flowcharts, ADR-000…ADR-010, directory skeleton, CI); hardware
  research — 9 component guides, build book, BOM, KiCad 9 PCB guide.

## [v0.0.1-phase0] — 2026-04-19

### Added
- Phase 0: WSL2 Debian environment — CUDA, cuDNN, TensorRT,
  OpenCV (CUDA source build) — verified by `smoke_test.cu`;
  repo bootstrap, license (GPL-3.0), CI workflows.

[Unreleased]: https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System/compare/v1.0.0-phase6...develop
[v1.0.0-phase6]: https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System/compare/v0.6.0-phase5...v1.0.0-phase6
[v0.6.0-phase5]: https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System/compare/v0.5.0-phase4...v0.6.0-phase5
[v0.5.0-phase4]: https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System/compare/v0.4.1-proto...v0.5.0-phase4
[v0.4.1-proto]: https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System/compare/v0.2.0-phase3...v0.4.1-proto
[v0.2.0-phase3]: https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System/compare/v0.0.1-phase0...v0.2.0-phase3
[v0.0.1-phase0]: https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System/releases/tag/v0.0.1-phase0
