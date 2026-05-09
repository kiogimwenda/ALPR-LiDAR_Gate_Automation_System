# ALPR + LiDAR Automated Vehicle Gate Control System

A production-grade automated vehicle gate controller that fuses **Automatic
License Plate Recognition (ALPR)** with **3D LiDAR** vehicle classification
to authorize or deny entry. ALPR runs YOLOv9 for plate detection and
PaddleOCR for character recognition, both accelerated via TensorRT on
NVIDIA GPUs. A 3D LiDAR pipeline classifies the vehicle independently. A
custom 4-layer ESP32-S3 field PCB drives the gate motor and senses
limit/safety inputs. A Drogon + SvelteKit dashboard provides real-time
monitoring, manual override, and audit logs.

**Repository:** [github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System](https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System)
**License:** [GPL-3.0](LICENSE) — rationale in [ADR-000](docs/decisions/ADR-000-license.md)

---

## Why this project exists

Residential and small-commercial gate access in Kenya (and most emerging
markets) is currently solved by either a guard with a clipboard or a remote
clicker. Both fail at the same things: tailgating, lost remotes, unrecorded
entries, and zero auditability. This project is a self-hosted, on-prem
alternative that:

- **Recognizes plates from real cameras at the gate** (Hikvision 4 MP PoE),
  not curated benchmarks.
- **Cross-checks plate against vehicle class** via 3D LiDAR — a sedan with
  a truck's plate is denied even if OCR succeeds. This is the LiDAR-fusion
  story that makes the system tailgating- and clone-resistant.
- **Runs entirely on a single GPU server on the local LAN** (RTX 4060 8 GB)
  — no cloud round-trip, no monthly per-gate fees.
- **Is repairable in the field** — every component is sourced from
  Luthuli Avenue (Nairobi) or AliExpress, the BOM is published, and the
  PCB is hand-solderable with documented through-hole points for the
  high-current paths.

---

## High-level architecture

```
┌─────────────────────────── AT THE GATE ──────────────────────────────────┐
│                                                                          │
│   Hikvision 4MP camera ──┐                                              │
│   Unitree L1 LiDAR  ─────┤  PoE switch ──── Ethernet ──── GPU server   │
│   ESP32-S3 field PCB ────┤      │                          │             │
│   Limit switches × 2 ────┘      │                          │             │
│   Photoelectric beam            │                          │             │
│                                                            │             │
│        Relay outputs to gate motor (CENTURION D5/R5)       │             │
│                                                            │             │
└────────────────────────────────────────────────────────────┼─────────────┘
                                                             │
┌─────────────────────────── SERVER ROOM ─────────────────────┼────────────┐
│                                                            │             │
│   GPU server: ALPR (YOLOv9 + PaddleOCR, TensorRT) +       │             │
│   LiDAR pipeline + fusion engine + gRPC server +          │             │
│   Drogon dashboard backend + SvelteKit dashboard frontend │             │
│                                                            │             │
└────────────────────────────────────────────────────────────┴─────────────┘
```

Full subsystem flowcharts: [docs/diagrams/](docs/diagrams/) (7 Mermaid files).

---

## Development timeline

The project executes in five sequential phases. Each phase has a tagged
release on GitHub.

| Phase | Title | Status | Highlights |
|---|---|---|---|
| **0** | Environment bootstrap | ✅ Complete | WSL2 Debian + CUDA 13.1 + cuDNN 9.19 + TensorRT 10.15 + OpenCV 4.14 (CUDA source-build) verified by `smoke_test.cu`. |
| **1** | Clarifications & decisions | ✅ Complete | All 12 design questions answered; defaults accepted; gate type override applied. |
| **2** | Architecture | ✅ Complete | 7 Mermaid flowcharts, 11 ADRs (ADR-000…ADR-010), full directory skeleton, CI workflows. |
| **3** | Hardware research & build guides | ✅ Complete | 9 component guides + master build book + complete BOM + 48-page KiCad 9.0 PCB design guide (PDF). |
| **4** | Implementation | 🔵 **In progress** — see below | Modern C++20 server, ESP-IDF firmware, simulation, dashboard. |
| 5 | Delivery | ⏳ Pending | Runbook, commissioning checklist, demo script, public release. |

### Phase 4 sub-milestones (current)

| # | Milestone | Status |
|---|---|---|
| 4.1 | gRPC wire contract (`shared/proto`) | ✅ Complete |
| **4.2** | **Server inference (TensorRT engines for YOLOv9 + PaddleOCR)** | ✅ **Complete** |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.1 | TrtEngine RAII wrapper around TensorRT 10.x | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.2 | YOLOv9 plate detector | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.3 | PaddleOCR character recognizer | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.4 | ALPR pipeline orchestrator | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.5 | Python ONNX → TensorRT conversion tooling | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.6 | Catch2 inference unit tests | ✅ Complete |
| **4.3** | **Server RPC + fusion engine** | ✅ **Complete** |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.1 | Allowlist + blocklist store (SQLite) | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.2 | Fusion engine (verdict ladder) | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.3 | Dashboard event broadcaster | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.4 | gRPC server + Dashboard / Admin services | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.5 | FieldControllerService + main.cpp | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.6 | Integration tests + Phase 4.3 closure | ✅ Complete |
| **4.4** | **Firmware drivers (W5500, relays, sensors)** | ✅ **Complete** |
| &nbsp;&nbsp;&nbsp;&nbsp;4.4.1 | ESP-IDF project skeleton + hello-world build | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.4.2 | GPIO drivers: relays + limit switches | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.4.3 | W5500 ethernet driver wrapper | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.4.4 | Safety beam input + LED status driver | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.4.5 | Firmware CI workflow + Phase 4.4 closure | ✅ Complete |
| **4.5** | **Firmware app (state machine, gRPC client, OTA)** | 🔵 **In progress — next** |
| 4.6 | Simulation harness | ⏳ Pending |
| 4.7 | Dashboard backend + frontend | ⏳ Pending |
| 4.8 | Deployment scripts (systemd, install) | ⏳ Pending |
| 4.9 | End-to-end integration tests | ⏳ Pending |

---

What follows is the chronological log of every milestone — Phase 0
(environment bootstrap) through the latest closure. Each section
captures what shipped, why the design decisions were made, and what was
deliberately left for later. Read top-to-bottom for the build story; the
newest entry is at the bottom.

---

## Phase 0 — Environment bootstrap

> **Completed 2026-04-19.** Audit at
> [`docs/env-audit.md`](docs/env-audit.md); install scripts in
> [`scripts/bootstrap/`](scripts/bootstrap/).

### What I built

A reproducible install of the entire C++/CUDA/ML toolchain on WSL2
Debian, audited end-to-end with a `smoke_test.cu` that compiles for
both `sm_120` (the dev box's RTX 5060) and `sm_89` (the production
RTX 4060) and runs a CUDA kernel under each — proving the install
isn't just present but actually executes on the target architectures.

| Artifact | Purpose |
|---|---|
| `scripts/bootstrap/01-toolchain.sh` | Installs the C/C++ toolchain — GCC 14, Clang 19, CMake ≥3.28, Ninja, ccache, mold, pkg-config, git-lfs. Idempotent: re-runs are a no-op. |
| `scripts/bootstrap/02-gpu-stack.sh` | Installs the NVIDIA stack — CUDA 13.1 toolkit, cuDNN 9.19, TensorRT 10.15 dev. Verifies `nvcc -arch=sm_120` and `sm_89` both compile and run. |
| `scripts/bootstrap/03-ml-tools.sh` | Builds OpenCV 4.14 from source with the CUDA modules (`cudaarithm`, `cudaimgproc`, `cudafilters`, `cudacodec`) installed to `/usr/local/lib/`. Pip's OpenCV package is fine for export scripts but unusable for the inference path. |
| `scripts/bootstrap/04-vcpkg.sh` | Pins vcpkg to a specific commit and primes the binary cache at `~/.cache/vcpkg`. |
| `scripts/bootstrap/smoke_test.cu` | The execution proof — a CUDA kernel built for both target archs and run on the dev box. Without this, "TensorRT 10.15 installed" is unverified marketing. |

### Technical detail

#### Why source-build OpenCV instead of the pip wheel

The pip `opencv-contrib-python` wheel ships with no CUDA modules at
all — every `cv::cuda::*` symbol resolves to a stub that throws
"OpenCV was built without CUDA support". The inference pipeline
copies camera frames into GPU memory and runs detection / classification
on-device, so CUDA OpenCV is non-negotiable. The bootstrap script
clones OpenCV + opencv_contrib at the same tag, configures with
`-D WITH_CUDA=ON -D OPENCV_DNN_CUDA=ON -D CUDA_ARCH_BIN="8.9;12.0"`,
and installs the resulting `.so`s to `/usr/local/lib/`. The vcpkg
manifest then finds them via system pkg-config.

The pip Python build remains useful for the `tools/export-models/`
scripts (those convert ONNX → TensorRT engines on the host CPU + Python's
`tensorrt` module), so both coexist intentionally.

#### Two CUDA architectures

`sm_120` is the dev box's Blackwell (RTX 5060 Laptop), `sm_89` is the
production Ada Lovelace (RTX 4060 server). Compiling for both means
the same engine binary will load on either GPU without a JIT step.
TensorRT engine plans, by contrast, are arch-specific and will be
re-built on the production server in Phase 4.2.5 — that's expected.

The smoke test compiles **and runs** under each arch, not just compiles.
A failed install would compile cleanly then crash at kernel launch with
"no kernel image is available for execution"; the runtime check catches
that before it bites in Phase 4.

#### nvidia-smi vs nvcc version drift

`nvidia-smi` reports CUDA 13.2 (driver capability); `nvcc` is 13.1
(toolkit installed). The driver supports up to 13.2 but the toolkit is
the one that compiles code, so the audit treats `nvcc --version` as
the source of truth. This shows up in the env-audit table and is
called out so future engineers don't chase a phantom mismatch.

#### What's intentionally not bootstrapped

- ESP-IDF toolchain — installed once at `~/esp/esp-idf` outside the
  repo because it's a 1.5 GB clone and changing IDF versions is a
  rare, manual operation.
- KiCad 9.0 — desktop tool, installed via the OS package manager.
- Python venvs — `~/ml-env/` is created out-of-band and activated by
  the user's `.bashrc`. The repo's `tools/` and `scripts/` work inside
  whichever venv is active.

---

## Phase 1 — Clarifications & decisions

> **Completed 2026-04-19.** Decisions logged in conversation memory
> and applied as design constraints from Phase 2 onward.

### What I built

Twelve project-defining questions, answered up front. The point of
Phase 1 isn't to write a document — it's to make every later phase
buildable without coming back and asking "wait, does this need to
support boom barriers too?" or "is the dashboard internet-facing?"
Defaults were accepted on every question with one deliberate override
(actuator type), and the answers became binding constraints for
Phase 2's architecture work.

| Question | Decision |
|---|---|
| **1. Site profile** | Single residential estate gate (prototype), scaling later to multi-gate deployments. |
| **2. Vehicle classes** | Cars + SUVs/pickups primary; trucks secondary; motorbikes excluded from this prototype. |
| **3. Plate format** | Kenyan NTSA only — both the new generation (white background, black text) and the legacy yellow rear plates. |
| **4. Auth model** | Allowlist + blocklist + time windows + visitor pre-registration + guard manual override. Standalone (no central directory dependency). |
| **5. Power-loss behavior** | Fail-safe — gate **opens** on power loss. (Residential safety requirement; commercial deployments may invert this.) |
| **6. Gate actuator** | **Override:** sliding gates and dual-leaf swing gates (residential Kenya pattern), **not** boom barriers. Boom barriers added later for commercial/industrial sites. |
| **7. Network** | Same LAN as the GPU server, dedicated VLAN, sub-millisecond latency assumed. |
| **8. Budget** | ~150,000 KES per gate (~USD 1,150) + ~200,000 KES for the central server (~USD 1,550). |
| **9. Regulatory** | Local-only storage, 90-day retention, hashed plates in long-term audit logs, Subject Access Request endpoint. |
| **10. Simulation** | All three modes — replay (recorded camera + LiDAR), synthetic (procedurally generated), hardware-in-the-loop. |
| **11. OTA** | Self-hosted HTTPS + ed25519 signing, atomic flash with rollback. (No vendor cloud, no app-store update flow.) |
| **12. Dashboard auth** | LAN-only, HTTP basic auth over TLS with self-signed certificates. |

### Technical detail

#### Why the actuator override matters

The default question template assumed boom barriers — single-arm
counterweighted gates common at parking-lot entries. Residential Kenya
uses sliding gates (single-rail or telescopic) and dual-leaf swing
gates almost exclusively. The override flipped six downstream
decisions:

- **Limit-switch count** — sliders/swings need 2 (open + closed) per
  leaf; booms need 1.
- **Relay count** — dual-leaf swings need 2 outputs (one per leaf,
  with a synchronization delay); sliders and booms need 1.
- **Safety-beam placement** — across the path of travel, not at the
  arm pivot.
- **Motor type** — Centurion D5 (slider) or R5 (swing), not a Faac
  boom kit. Mains AC, not 24 V DC.
- **Travel time** — 8–15 s for a 4 m slider vs. 1–2 s for a boom.
  This is what made `relay::pulse(duration)` and the limit-switch
  poll interval worth getting right in Phase 4.4.2.
- **Fail-safe semantics** — sliders and swings can be physically
  pushed open during power loss (residents' expectation in KE);
  booms cannot.

The override was applied to all subsequent phases. Phase 4.4's
driver suite (Relay, LimitSwitch, SafetyBeam) has the dual-leaf
case as a first-class scenario, not an afterthought.

#### What "defaults accepted" actually means

Every other question's default came from the same mental model — a
self-hosted, on-prem, LAN-only system aimed at a Kenyan residential
estate or small commercial site with no reliable internet uplink.
Accepting the defaults was a deliberate ratification, not skipping
the questions: the bandwidth assumption (Q7), the budget cap (Q8),
the on-device storage (Q9), and the LAN-only dashboard (Q12) all
hang together. Changing one would force re-answering several others.

#### Why this lives in memory, not a doc file

The clarification answers are durable design constraints, not
versionable documents. They were saved to project memory so every
future Claude Code session in this repo loads them automatically and
applies them as constraints — without the user having to re-explain
"sliding gates, not booms" every time a new phase opens.

A `docs/clarifications.md` file would have been a one-time write
that immediately falls out of sync with the actual decisions baked
into ADRs. Keeping the answers in memory makes them living context.

---

## Phase 2 — Architecture

> **Completed 2026-04-19.** Diagrams in
> [`docs/diagrams/`](docs/diagrams/) (7 Mermaid files); decisions
> in [`docs/decisions/`](docs/decisions/) (11 ADRs); CI workflows
> in [`.github/workflows/`](.github/workflows/).

### What I built

The spine that every later phase hangs off: subsystem flowcharts,
eleven Architecture Decision Records that pin every framework /
hardware choice with reasoning and alternatives, the full repo
directory scaffold, the vcpkg + CMake-presets build skeleton, and
six CI workflows wired up before any feature code shipped — so the
first commit of real logic in Phase 4 already had `-Werror` and
`clang-format` enforced from line one.

| Artifact | Purpose |
|---|---|
| `docs/diagrams/01-system-level.mmd` | Top-level data flow: cameras / LiDAR → GPU server → ESP32 field PCB → gate motor. |
| `docs/diagrams/02-alpr-subsystem.mmd` | YOLOv9 detection → ROI crop → PaddleOCR → plate normalization → fusion engine input. |
| `docs/diagrams/03-lidar-subsystem.mmd` | Unitree L1 frame → point cloud filtering → bounding box → vehicle classification → fusion input. |
| `docs/diagrams/04-fusion-decision.mmd` | The verdict ladder: allow / deny / require-second-source / hold-for-guard. |
| `docs/diagrams/05-gate-state-machine.mmd` | IDLE → AUTHORIZING → OPENING → OPEN → CLOSING → FAULT, with safety-beam interrupts. |
| `docs/diagrams/06-ota-update.mmd` | DeliverOta chunk stream → flash inactive partition → verify → mark-bootable → reboot. |
| `docs/diagrams/07-simulation-mode.mmd` | Replay / synthetic / HIL paths and how each plugs into the same fusion + RPC code. |
| `docs/decisions/ADR-000…ADR-010` | Eleven decision records — license, package manager, RPC, web framework, frontend, MCU, LiDAR, camera, actuator interface, OTA, model licensing. |
| `.github/workflows/{build,test,lint,proto,codeql,commitlint}.yml` | Six CI workflows — host build matrix, sanitizer tests, clang-format / clang-tidy / cppcheck, proto descriptor validator, GitHub CodeQL security scan, conventional-commit lint. |
| `vcpkg.json` + `CMakePresets.json` | Locked dependency manifest + named build presets (`gcc-debug`, `gcc-release`, `clang-debug`, `clang-release`). |

### Technical detail

#### Why ADRs, not a wiki

Eleven ADRs sit in `docs/decisions/` as immutable, dated, numbered
markdown files. Each one names the decision, the alternatives
considered, the consequences, and (where relevant) what would force
revisiting it. ADRs survive when the wiki gets archived; they live
next to the code in version control and review under the same PR
gate. The ADR set:

- **000** — License (GPL-3.0 — chosen because YOLOv9's GPL-3.0 weights propagate; full reasoning in ADR-010).
- **001** — Package manager (vcpkg, not Conan or Hunter — manifest mode + binary cache + better Windows story).
- **002** — RPC mechanism (gRPC, not REST or raw TCP — bidi streams for telemetry, schema-first, language coverage).
- **003** — Web framework for dashboard backend (Drogon C++ — same toolchain as the inference server, no Node runtime in the deploy).
- **004** — Frontend framework (SvelteKit — small bundle, server-rendered fallbacks for edge devices).
- **005** — MCU / SoC choice (ESP32-S3 — dual-core 240 MHz Xtensa, PSRAM option, mature ESP-IDF).
- **006** — LiDAR sensor (Unitree L1 PM — 4D 360° × 90° at the price point that fits the budget).
- **007** — ALPR camera (Hikvision DS-2CD2043G2-I — 4 MP IR with PoE, available on Luthuli Avenue).
- **008** — Gate actuator interface (relay outputs, not Modbus / RS-485 — every Centurion D5/R5 controller has dry-contact inputs).
- **009** — OTA strategy (self-hosted HTTPS + ed25519 — no vendor cloud, atomic with rollback).
- **010** — Model licensing (YOLOv9 GPL-3.0 implications drove the project license choice in ADR-000).

#### Why CI before features

The six workflows landed in commit `c242943` — before any first-party
C++ code. That ordering is deliberate: by the time Phase 4.1 shipped
the first proto file, the lint workflow was already enforcing
clang-format, the build workflow already had the gcc/clang × debug/release
matrix, and the proto workflow already validated the descriptor. Every
feature commit since then has had to pass that same gate from the
moment it was authored — there's no "we'll add CI later" technical
debt in this repo.

The matrix isn't decorative: gcc-13 + gcc-14 + clang-17 + clang-18,
each in Debug and Release, eight builds per push. Different compilers
flag different bugs (gcc finds use-after-move, clang finds switch-coverage
gaps); running both has caught real issues across Phase 4.

#### Directory scaffold up front

`server/`, `firmware/`, `dashboard/`, `simulation/`, `hardware/`,
`docs/`, `scripts/`, `tools/`, `tests/`, `shared/proto/`, `deployment/` —
all present at the end of Phase 2 with placeholder `README.md` files and
empty subdirectories. New work goes into a known location. There has
not been a single "where should this file live?" question across all
of Phase 4.

#### Mermaid for diagrams

`.mmd` files render natively on GitHub and re-render automatically when
the markdown is edited. PNG / SVG diagrams would lock the source to
whoever has the design tool installed; Mermaid lets a future contributor
edit the architecture from a text editor and see the result in the PR
diff. The seven flowcharts haven't drifted from the implementation
because editing the Mermaid is part of the PR workflow.

---

## Phase 3 — Hardware research & build guides

> **Completed 2026-04-20.** Component guides + master build book in
> [`docs/hardware/`](docs/hardware/); BOM at
> [`hardware/bom/prototype-bom.md`](hardware/bom/prototype-bom.md);
> 48-page PCB design guide at
> [`docs/hardware/10-pcb-design-kicad9.pdf`](docs/hardware/10-pcb-design-kicad9.pdf).

### What I built

Nine per-component build guides, one master build book that walks
through full system assembly, a complete bill of materials with both
KES and USD pricing tied to actual Nairobi suppliers, and a
48-page KiCad 9.0 design guide for the custom 4-layer field PCB.
Phase 3 turned the architecture decisions from Phase 2 into
buildable parts lists — by the end of it, every component had a
specific model number, a known supplier, a wiring diagram, and a
documented gotcha list.

| Artifact | Purpose |
|---|---|
| `docs/hardware/01-esp32-s3-devkit.md` | DevKitC-1-N16R8 setup — pinout, USB-C vs UART, flashing recipe, current draw. |
| `docs/hardware/02-w5500-ethernet.md` | W5500 SPI module — pinout, 3.3 V vs 5 V level shifting, MAC byte fuse. |
| `docs/hardware/03-unitree-l1-lidar.md` | Unitree L1 PM — Ethernet config, ROS driver vs raw UDP, mounting angle for vehicle classification. |
| `docs/hardware/04-hikvision-camera.md` | DS-2CD2043G2-I — PoE wiring, RTSP URL pattern, focal-length math for plate readability at gate distance. |
| `docs/hardware/05-relay-module.md` | 4-channel optoisolated relay — coil current, contact rating vs gate-motor inrush, wiring to Centurion D5/R5 dry-contact inputs. |
| `docs/hardware/06-safety-sensors.md` | Photoelectric beam pair (TX+RX) + magnetic reed limit switches — failsafe wiring, NO vs NC, IP rating notes. |
| `docs/hardware/07-power-supply.md` | 12V/2A enclosed PSU + LM2596 12V→5V buck — derating, ripple, fuse sizing. |
| `docs/hardware/08-network-switch.md` | TP-Link TL-SG1005P PoE — VLAN config (per ADR-008's dedicated VLAN constraint), PoE budget math. |
| `docs/hardware/09-gpu-server.md` | Production server BOM — RTX 4060, Ryzen 5, 32 GB RAM, NVMe — and why this combination fits the inference workload at the budget. |
| `docs/hardware/BUILD_BOOK.md` | Master assembly walkthrough — unboxing → first power-on → smoke test, in order. |
| `docs/hardware/10-pcb-design-kicad9.{html,pdf}` | 48-page step-by-step KiCad 9.0 guide for the custom 4-layer field PCB — schematic capture, footprint selection, layer stack, copper pours, DRC, gerber export, JLCPCB upload. |
| `hardware/bom/prototype-bom.md` | Per-line BOM with KES + USD pricing, supplier names, and a single-gate subtotal. |

### Technical detail

#### Why source from Luthuli Avenue + AliExpress

Luthuli Avenue is the electronics-heavy commercial street in Nairobi
where every component on the BOM is physically buyable today. AliExpress
fills the gaps for items the local market doesn't stock (Unitree L1,
specific Hikvision SKUs). This sourcing constraint was set in Phase 1
(Q8: budget) and Phase 1 (Q9: deployment in Kenya) and shaped every
component choice — the W5500 module is the cheap one because that's
what Luthuli stocks; the LiDAR is Unitree because Velodyne / Ouster
sit at 10× the price; the camera is a Hikvision DS-2CD2043G2-I because
that's what every CCTV shop on Luthuli has on the shelf.

A different deployment context (US, EU, datacenter) would pick
different parts. The BOM lives in version control specifically so a
fork can rewrite it without forking the rest of the project.

#### Why a custom PCB instead of a perfboard

The prototype could in principle be wired on a perfboard — every
component is module-format with header pins. The PCB exists for
three reasons:

1. **EMI on the SPI lines.** The W5500's 25 MHz SPI clock pulls
   current spikes that couple into adjacent unshielded jumpers; the
   PCB lays them as controlled-impedance traces on an inner layer
   between ground planes.
2. **Mechanical durability.** Header-pin connections vibrate loose
   in an enclosure mounted on a moving slider gate. The PCB uses
   screw terminals for everything that leaves the enclosure (relay
   outputs, limit-switch inputs, safety-beam, 12 V power).
3. **Repairability.** ADR-008 commits to "every solder joint
   accessible with a 30 W iron." The 48-page design guide bakes
   that constraint in — through-hole for the high-current paths
   (relay outputs, 12 V rail), SMD only for the small-signal stuff
   (W5500, ESP32 footprint).

#### What's not in Phase 3

- **No PCB ordered yet** — the design guide produces the gerbers but
  the prototype is currently breadboarded. PCB fab + assembly is in
  Phase 4.9 (end-to-end integration tests on real hardware).
- **No enclosure CAD** — the IP65 junction box is off-the-shelf.
  Custom enclosure design (if needed for production) is a Phase 5
  decision.
- **No multi-gate BOM** — the prototype BOM is for a single gate.
  Multi-gate scaling math (PoE switch capacity, GPU inference
  throughput, dashboard concurrent connections) is a Phase 5 / 4.9
  topic.

The point of Phase 3 was to make the prototype buildable end-to-end
on the prototype budget, which it does. Production hardening is
explicitly downstream.

---

## Phase 4.1 — gRPC wire contract

> **Pulled this off on 2026-04-25.** Full code in
> [`shared/proto/`](shared/proto/) and [`tests/proto/`](tests/proto/).

### What I built

A single canonical `gate.v1` protobuf schema —
[`shared/proto/gate_service.proto`](shared/proto/gate_service.proto) — that
defines every byte that crosses a process boundary in the system. Firmware,
inference server, dashboard backend, and simulation all generate stubs from
this one file.

| Artifact | Purpose |
|---|---|
| `shared/proto/gate_service.proto` | 25 messages, 6 enums, 3 services, 10 RPC methods. |
| `shared/proto/CMakeLists.txt` | Generates `gate_proto` static lib (messages + gRPC stubs) for host C++ targets. Consumed by server, dashboard, and sim. |
| `shared/proto/README.md` | Developer reference for the wire contract. |
| `tests/proto/proto_contract_test.cpp` | Catch2 round-trip tests for telemetry, decisions, OTA, and the bidirectional `ControlEnvelope`. |
| `tests/proto/validate_descriptor.py` | protoc-only descriptor validator that runs in CI without the full C++ toolchain. |
| `.github/workflows/proto.yml` | CI job that compiles the descriptor and runs the validator on every change. |

### Technical detail

#### Service surface

Three services with deliberately chosen streaming patterns:

| Service / Method | Streaming kind | Why |
|---|---|---|
| `FieldControllerService.Control` | bidi (client+server stream) | One TCP connection per gate, kept alive for the lifetime of the field controller. Telemetry flows up at 1 Hz; commands flow down on demand; acks/faults multiplex over the same stream — half the connection count, no race between an incoming command and an outgoing ack. |
| `FieldControllerService.DeliverOta` | server stream | OTA images are 1.5–2 MB. Streaming 4 KB chunks (one ESP32 flash sector each) lets the firmware hash incrementally and abort early on signature mismatch without buffering the whole image in PSRAM. |
| `FieldControllerService.ReportOtaProgress` | client stream | Firmware emits a progress event for each phase (`DOWNLOADING → VERIFYING → INSTALLING → REBOOTING → COMPLETE`). One stream per OTA session. |
| `FieldControllerService.SubmitDetection` | unary | A single detection frame from the GPU pipeline returns one `AuthDecision`. Used inside the server and from the simulation harness. |
| `DashboardService.Subscribe` | server stream | The web UI gets a filtered, replayable feed of every `DashboardEvent` (decisions, telemetry, faults, OTA progress, commands, acks) via the `since_event_id` resume cursor. |
| `DashboardService.IssueCommand` / `Authorize` | unary | Manual override and synchronous test calls. |
| `AdminService.{Upsert,List,Delete}Allowlist` | unary | CRUD over the per-site allowlist. |

#### Key design choices

1. **`oneof ControlEnvelope.payload`** multiplexes telemetry, command,
   command-ack and fault events over the bidirectional stream. The contract
   test asserts the four payload tags and their stable field numbers
   (`kTelemetry`, `kAck`, `kFault`, `kCommand`).

2. **`google.protobuf.Timestamp` + `Duration` everywhere** — no integer
   "milliseconds since epoch" or hand-rolled time fields. This keeps every
   client (C++, Python sim, JS dashboard) using the same time semantics.

3. **`DashboardEvent.event_id` is monotonic** per server boot; the
   `DashboardSubscription.since_event_id` field lets the web UI resume after
   a refresh without losing events.

4. **`OtaChunk` size is fixed at 4096 bytes** to match an ESP32 flash sector
   (the contract test verifies a full sector round-trips). This eliminates
   the partial-sector edge case in firmware OTA.

5. **`Telemetry` is bandwidth-budgeted at 256 bytes**. The contract test
   `Telemetry stays under 256-byte firmware budget` enforces this at build
   time so we don't accidentally bloat the over-the-air protocol when
   adding fields.

6. **Stable field numbers and enum values** are documented in
   `validate_descriptor.py`. Any change that moves a number is a breaking
   change, requires a `gate.v2` package, and must be approved via ADR.

#### Build integration

`shared/proto/CMakeLists.txt` invokes `protoc` with the gRPC C++ plugin to
emit `gate_service.pb.{h,cc}` and `gate_service.grpc.pb.{h,cc}`, links them
into a single `gate_proto` static target, and re-exports the FileDescriptorSet
so other tools (gRPC reflection, dashboard codegen) can consume it. The
target is added by the root `CMakeLists.txt` whenever
`-DBUILD_PROTO=ON` (the default).

Generated code is compiled with `-Wno-unused-parameter -Wno-deprecated-declarations`
because `protoc` output is not under our control; first-party code remains
under the strict project-wide `-Wall -Wextra -Wpedantic -Werror`.

#### Validation strategy

Two layers, deliberately split:

- **`validate_descriptor.py`** runs in CI on every push that touches
  `shared/proto/`. It only needs `protoc` and Python's `google.protobuf` —
  no vcpkg toolchain bootstrap. It walks the descriptor and asserts:
    - All 25 expected messages exist.
    - All stable enum values have their documented numbers.
    - All 10 RPC methods exist with the correct client/server streaming kinds.
    - `ControlEnvelope` is a `oneof` named `payload` with the four
      documented variants.
- **`proto_contract_test.cpp`** is a Catch2 suite that round-trips every
  important message through `SerializeToString` / `ParseFromString`,
  verifies the `ControlEnvelope` oneof discriminator, and enforces the
  256-byte telemetry budget. It builds as part of the standard `tests/`
  target once the full vcpkg dependencies are in place.

#### Current validator output

```
Validating shared/proto/gate_service.proto
OK: package=gate.v1, syntax=proto3
OK: all 25 expected messages present
OK: enum VehicleClass has 9 stable values
OK: enum GateState has 7 stable values
OK: enum AuthVerdict has 5 stable values
OK: service FieldControllerService has 4 methods with correct streaming kinds
OK: service DashboardService has 3 methods with correct streaming kinds
OK: service AdminService has 3 methods with correct streaming kinds
OK: ControlEnvelope oneof has 4 expected payloads

All proto contract checks passed.
```

---

## Phase 4.2.1 — TrtEngine RAII wrapper

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/trt_engine.hpp`](server/inference/include/inference/trt_engine.hpp)
> and [`server/inference/src/trt_engine.cpp`](server/inference/src/trt_engine.cpp).

### What I built

A modern C++20 RAII wrapper around the TensorRT 10.x runtime API. It owns the
full inference state for one engine — the deserialized `ICudaEngine`, an
`IExecutionContext`, a CUDA stream, and per-binding device buffers — and
exposes a name-based, exception-throwing interface that the rest of the
inference layer (YOLOv9 detector, PaddleOCR recognizer, ALPR pipeline) is
built on top of.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/trt_engine.hpp` | Public API: `TrtException`, `TrtLogger`, `TensorIo` descriptor, move-only `TrtEngine` class. |
| `server/inference/src/trt_engine.cpp` | Implementation: engine deserialization, binding introspection, dynamic-shape buffer sizing, async H↔D + `enqueueV3` execution path. |
| `server/inference/CMakeLists.txt` | Builds `gate_inference` static lib; locates TensorRT headers/libs via standard system paths or `-DTENSORRT_ROOT=…`. |
| `server/CMakeLists.txt` | Adds the `inference/` subtree under `BUILD_SERVER=ON`. |
| Root `CMakeLists.txt` | Conditionally pulls in `server/` only when CUDA + spdlog are resolved, mirroring the same graceful-skip pattern used for `shared/proto/` and `tests/`. |

### Technical detail

#### Why a hand-written wrapper

TensorRT's runtime API (`IRuntime`, `ICudaEngine`, `IExecutionContext`)
returns raw pointers, uses noexcept return-code error handling, and in TRT
10.x deletes via the C++ `delete` operator (the older `destroy()` virtual is
gone). Calling that surface directly from inference code would scatter
`if (!ok) return false;` checks through every detector and recognizer. The
wrapper:

- Concentrates all error handling at one boundary — every TRT failure
  becomes a `gate::inference::TrtException` with a descriptive message
  (failing call name + offending tensor + offending shape where relevant).
- Takes ownership of the runtime / engine / context / CUDA stream / device
  buffers via `unique_ptr` with custom deleters, so the destructor frees
  resources in the only correct order: device buffers → stream → context →
  engine → runtime.
- Exposes name-based binding (`engine->getIOTensorName(i)` /
  `setTensorAddress(name, ptr)`) so re-exporting an engine with renumbered
  bindings doesn't break call sites — they reference tensors by string.
- Is move-only and noexcept-movable, which lets it live inside containers
  (`std::vector<TrtEngine>`) for the multi-engine pipeline that Phases 4.2.2
  and 4.2.3 will assemble.

#### Engine load path

`TrtEngine::load(path, logger)`:

1. Reads the serialized `.plan` file into a `std::vector<std::byte>` in one
   shot. The plan is opaque bytes; we do not parse it — TensorRT does.
2. `nvinfer1::createInferRuntime(logger)` produces the runtime. The supplied
   logger is a TensorRT `ILogger` reference; the project's default
   implementation, `TrtLogger`, bridges TRT severities to spdlog levels
   (`kINTERNAL_ERROR → critical`, `kERROR → err`, `kWARNING → warn`, etc.)
   with a configurable threshold (default `kWARNING` to keep INFO chatter
   out of the production log).
3. `runtime_->deserializeCudaEngine(blob.data(), blob.size())` reconstructs
   the engine. A null return raises `TrtException` with the engine path so
   plan corruption surfaces immediately at startup, never at the first
   inference.
4. `engine_->createExecutionContext()` produces the per-thread context.
5. A dedicated CUDA stream is created via `cudaStreamCreate`. Every H↔D
   copy and every kernel launch is enqueued on this stream, so `sync()` is
   the single observation point for all in-flight work.

#### Binding introspection

After the context is created, the loader walks
`engine_->getNbIOTensors()` and builds a `TensorIo` descriptor for each
binding:

```cpp
struct TensorIo {
    std::string             name;       // canonical TRT tensor name
    nvinfer1::Dims          shape;      // -1 marks dynamic dims
    nvinfer1::DataType      dtype;
    bool                    is_input;
    std::size_t             elem_size;  // bytes per element
};
```

Two parallel containers index by binding position (`tensors_`,
`device_buffers_`, `device_buffer_bytes_`); a `name_to_index_` hash map
gives O(1) name lookup. All public methods take `std::string_view` and
resolve through that map so call sites don't carry binding numbers.

#### Dynamic-shape device buffer allocation

The hardest part of a generic TRT wrapper is sizing buffers when the engine
has dynamic dimensions (the `-1` dims YOLOv9 uses for `batch` and the OCR
recognizer uses for sequence length). The wrapper handles all three cases
in `allocate_buffers_()`:

1. **Static binding** — `volume(shape) * elem_size` is allocated directly.
2. **Dynamic input** — the wrapper queries
   `engine_->getProfileShape(name, 0, OptProfileSelector::kMAX)` and
   allocates for that maximum. Subsequent `set_input_shape()` calls with
   any in-profile shape reuse the same buffer.
3. **Dynamic output** — output shapes can also be `-1` (e.g. NMS-derived
   detection counts). The wrapper does a second pass: it primes every
   dynamic input with its kMAX shape via `setInputShape`, then asks the
   context to resolve each output via `getTensorShape(name)`, and allocates
   from there.

`buffer_bytes(name)` consults the context's *current* view of the shape, so
after `set_input_shape()` the reported size shrinks to match the runtime
shape — the device buffer is over-allocated (safe) but `enqueue()` only
copies the bytes the model actually consumes/produces.

#### Async execution path

`enqueue(host_in, host_out)` performs the entire ALPR-step lifecycle on the
internal stream:

1. **Re-bind every tensor address.** TRT 10's `enqueueV3` requires an
   address for every input and output to have been set since the last
   `setInputShape` call. The wrapper rebinds unconditionally so callers
   don't accidentally inherit a stale binding from a prior context use.
2. **Async H→D for every input** in the supplied map.
   `cudaMemcpyAsync(..., cudaMemcpyHostToDevice, stream_)`. Each input span
   is size-checked against `buffer_bytes(name)`; mismatches throw before
   any DMA is issued.
3. **Async forward pass.** `context_->enqueueV3(stream_)`. A `false` return
   raises `TrtException` with a hint about the most likely cause (unbound
   input or unset shape).
4. **Async D→H for every requested output**, with the same size-check
   discipline.
5. **`sync()`** is a separate, optional call. The split lets the caller
   overlap CPU work (post-processing, fusion, gRPC reply assembly) with
   GPU work, and gives the ALPR pipeline a place to insert a CUDA event
   for cross-stream barriers later.

Every CUDA call goes through a `check_cuda(status, "what")` helper that
converts the error code into `cudaGetErrorString(...)` text so failures in
production logs are immediately diagnosable.

#### Build integration

`server/inference/CMakeLists.txt` locates TensorRT through `find_path` /
`find_library` rather than `find_package` because TensorRT does not ship a
CMake config. The result is wrapped in an `IMPORTED` target,
`TensorRT::nvinfer`, with the header path attached as `SYSTEM` includes so
the project-wide `-Wall -Wextra -Wpedantic -Werror` doesn't flag TRT's
own headers. The library links `CUDA::cudart` from `CUDAToolkit` and
`spdlog::spdlog` from vcpkg.

The root `CMakeLists.txt` only descends into `server/` when `BUILD_SERVER`
is on **and** `find_package(spdlog CONFIG QUIET)` succeeds. CI's
non-toolchain Build job stays green because the inference module is simply
skipped with a clear status message; local development with the vcpkg
toolchain pulls the full dependency closure (gRPC, protobuf, drogon,
spdlog, sqlite3, fmt, nlohmann-json, Catch2, cli11) and builds normally.

#### Verified compile

Built clean against:

- TensorRT 10.16.1 (`libnvinfer.so` at `/usr/lib/x86_64-linux-gnu/`)
- CUDA Toolkit 13.1.115 (nvcc + cudart)
- GCC 14.2.0 with `-std=c++20 -Wall -Wextra -Wpedantic -Werror`
- spdlog 1.17.0 from vcpkg

Build output:

```
[1/4] Scanning .../trt_engine.cpp for CXX dependencies
[2/4] Generating CXX dyndep file ...
[3/4] Building CXX object .../trt_engine.cpp.o
[4/4] Linking CXX static library libgate_inference.a
```

No warnings, no diagnostics — strict warnings are kept on for first-party
code.

---

## Phase 4.2.2 — YOLOv9 plate detector

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/yolo_plate_detector.hpp`](server/inference/include/inference/yolo_plate_detector.hpp)
> and [`server/inference/src/yolo_plate_detector.cpp`](server/inference/src/yolo_plate_detector.cpp).

### What I built

A single-camera YOLOv9 license-plate detector built directly on the
`TrtEngine` wrapper from 4.2.1. It takes a BGR `cv::Mat` and returns a
`std::vector<PlateDetection>` whose boxes are already in the original
image's pixel coordinate frame — the call site does not need to know
anything about model input size, letterboxing, or NMS.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/yolo_plate_detector.hpp` | Public API: `PlateDetection` struct, `YoloPlateDetector::Config`, move-only detector class. |
| `server/inference/src/yolo_plate_detector.cpp` | Implementation: letterbox, BGR→RGB normalization, CHW pack, async inference, EfficientNMS_TRT decode, coordinate unmap. |
| `server/inference/CMakeLists.txt` (updated) | Adds OpenCV (core + imgproc) to `gate_inference`'s public link line. |

### Technical detail

#### Why model-side NMS

The detector targets ONNX exports produced by the official `yolov9` repo
with the `--end2end` flag, which embeds the standard
`EfficientNMS_TRT` plugin in the model graph. The engine therefore emits
already-NMSed detections via four output tensors — `num_dets`,
`det_boxes`, `det_scores`, `det_classes` — and the C++ side never has to
implement anchor decoding or non-max suppression. This pushes
~2 ms of CPU work onto the GPU where it overlaps with the rest of the
forward pass, and keeps the call-site code under 200 lines.

The detector still applies a `confidence_floor` filter on the way back
out as defense-in-depth: the EfficientNMS thresholds are baked in at
export time, but the deployment may want a stricter floor without
re-exporting the engine.

#### Preprocessing pipeline

`YoloPlateDetector::detect(const cv::Mat& bgr)` does the standard
YOLO-family preprocessing in three OpenCV steps:

1. **Letterbox.** `letterbox_()` resizes the source frame to fit inside
   the model's input canvas (default 640×640) preserving aspect ratio,
   then pads the remainder with neutral gray `(114, 114, 114)` — the
   YOLOv9 / Ultralytics convention. The `(scale, pad_x, pad_y)` triple
   is captured so detection boxes can be unmapped exactly.
2. **Color + dtype.** `cv::cvtColor(... BGR2RGB)` then `convertTo(...,
   CV_32FC3, 1/255)`. Two function calls; OpenCV does the SIMD work.
3. **HWC → CHW.** `cv::split` writes the three planes directly into a
   contiguous `std::vector<float>` host buffer that the wrapper
   pre-allocated at `load()` time, so per-frame inference does no heap
   allocation in the hot path.

The host buffer is then handed to `TrtEngine::enqueue` as a
`std::span<const std::byte>` keyed by the input tensor name (`"images"`
by default).

#### Output decoding

`enqueue` is followed by a single `sync()` (we don't yet pipeline
detection with downstream OCR — that's 4.2.4 territory). The four
output buffers are then walked once:

```
for i in [0, num_dets[0]):
    score = scores[i]
    if score < confidence_floor: continue
    (x1, y1, x2, y2) = boxes[i*4 : i*4+4]      // letterboxed-input space
    x1 = max(0, (x1 - pad_x) / scale)          // → original-image space
    y1 = max(0, (y1 - pad_y) / scale)
    x2 = min(W, (x2 - pad_x) / scale)
    y2 = min(H, (y2 - pad_y) / scale)
    if x2 <= x1 or y2 <= y1: continue          // degenerate after clamp
    emit PlateDetection{box, score, class_id}
```

Detections are returned sorted by descending confidence so the ALPR
pipeline (4.2.4) can apply a top-K crop policy without resorting.

#### Allocation discipline

Five host scratch buffers (`input_chw_`, `num_dets_host_`, `boxes_host_`,
`scores_host_`, `classes_host_`) are sized once at `load()` from the
context-resolved output shapes and reused for every frame. The detector
makes zero allocations in the per-frame path beyond OpenCV's internal
working memory for the `cvtColor`/`convertTo`/`split` steps.

The `max_detections` constant comes from the engine itself —
`engine_->context()->getTensorShape("det_boxes")` returns
`[1, max_det, 4]` after the input shape is pinned at load time. This
keeps the C++ side automatically in sync with however the ONNX export
was configured.

#### CMake: surviving OpenCV-with-CUDA on a modern toolchain

OpenCV was source-built against CUDA 13.1, which means
`OpenCVConfig.cmake` unconditionally calls
`find_host_package(CUDA REQUIRED)` — the legacy `FindCUDA` module.
CMake 3.27+ defaulted policy `CMP0146` to `NEW`, which removes that
module, and the policy doesn't propagate through vcpkg's
`find_package` wrapper. Rather than fight scoping, the build pre-fills
the half-dozen `CUDA_*` variables that OpenCV's config actually reads —
sourced from the modern `CUDA::cudart` / `CUDA::cublas` /
`CUDA::cufft` / `CUDA::nppc` / `CUDA::nppial` / `CUDA::npps` imported
targets that `find_package(CUDAToolkit)` provides — and stubs out
`find_cuda_helper_libs` as a no-op since we've already populated the
libraries it would have found. OpenCV's `if(NOT CUDA_FOUND)` short-
circuits cleanly and the rest of its config proceeds normally.

This is documented inline in `server/inference/CMakeLists.txt` so the
next person who reads it doesn't have to re-derive the chain.

#### Verified compile

```
[1/6] Scanning .../trt_engine.cpp for CXX dependencies
[2/6] Scanning .../yolo_plate_detector.cpp for CXX dependencies
[3/6] Generating CXX dyndep file ...
[4/6] Building CXX object .../trt_engine.cpp.o
[5/6] Building CXX object .../yolo_plate_detector.cpp.o
[6/6] Linking CXX static library libgate_inference.a
```

Built against TensorRT 10.16.1, CUDA 13.1.115, OpenCV 4.14.0 (CUDA
source-build), GCC 14.2.0 with `-std=c++20 -Wall -Wextra -Wpedantic
-Werror` — no diagnostics.

---

## Phase 4.2.3 — PaddleOCR plate-text recognizer

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/paddle_ocr_recognizer.hpp`](server/inference/include/inference/paddle_ocr_recognizer.hpp)
> and [`server/inference/src/paddle_ocr_recognizer.cpp`](server/inference/src/paddle_ocr_recognizer.cpp).
> Dictionary asset at
> [`server/models/dict_kenya_plates.txt`](server/models/dict_kenya_plates.txt).

### What I built

A PaddleOCR PP-OCRv4-compatible plate-text recognizer that takes a
cropped plate image (the typical output of `YoloPlateDetector::detect`)
and returns a `RecognizedPlate { text, confidence }`. Like the
detector, it sits directly on top of the `TrtEngine` wrapper from
4.2.1 and uses the same async-enqueue / single-sync execution model so
the upcoming ALPR pipeline can chain detection → recognition without
ever reading the host CPU between models.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/paddle_ocr_recognizer.hpp` | Public API: `RecognizedPlate` struct, `PaddleOcrRecognizer::Config`, move-only recognizer class. |
| `server/inference/src/paddle_ocr_recognizer.cpp` | Implementation: aspect-preserving resize + right-pad, per-channel mean/std normalization, async TRT inference, greedy CTC decode. |
| `server/models/dict_kenya_plates.txt` | 36-character dictionary (0–9, A–Z) covering every character that can appear on a Kenyan civilian or government plate. |
| `server/models/README.md` | Explains what model assets live in the tree (dictionaries) vs. what is built locally and gitignored (`.plan` engines, `.onnx` exports). |

### Technical detail

#### Preprocessing — what PaddleOCR actually expects

PP-OCRv4's plate recognizer is a CRNN-style network: a CNN backbone
that emits a sequence of feature columns, fed into a CTC head. The
input contract is unusual:

- **Fixed input height (48 px)** — required, because the CNN backbone's
  vertical stride collapses height to 1 in the feature map.
- **Variable input width up to a maximum (320 px)** — the recognizer
  reads left-to-right, so wider crops give more time steps but the
  trained max is 320.
- **Aspect-preserving resize** — squashing a wide plate into a square
  destroys character geometry; the model is trained on aspect-preserved
  inputs zero-padded on the right.

`preprocess_()` does exactly that: resize so height = 48 and width =
`round(48 × aspect)` clamped to `[1, 320]`, then `copyTo` into a
`(48 × 320, BGR, zero-padded)` canvas. The CTC head treats those
zero-padded columns as low-energy time steps and decodes them as blanks,
which the post-processor strips — so padding has no semantic effect on
the output text.

#### Normalization

PP-OCRv4 was trained with `(pixel/255 - mean) / std`, default
`mean = std = (0.5, 0.5, 0.5)`. With those symmetric values the
BGR-vs-RGB channel order is irrelevant, so the recognizer reads
OpenCV's native BGR directly and avoids a `cvtColor` round trip. The
arithmetic is per-channel (`cv::split` → subtract → divide), which keeps
us off `opencv_dnn::blobFromImage` and shaves a heavy module out of the
link line.

#### CTC greedy decode

The recognizer's output is `[1, T, C]` post-softmax probabilities. The
decoder is the standard CTC greedy:

```
for each time step t in [0, T):
    c = argmax_c output[t, c]
    if c == 0 (blank) or c == prev: skip            # CTC blank + repeat collapse
    text   += dictionary[c - 1]
    conf   += output[t, c]
    prev   = c
return (text, conf / kept_count)
```

Per-character confidence is the argmax probability at that time step;
overall plate confidence is the mean of those per-character values.
This is the right summary statistic for a downstream allow-list match —
a single low-confidence character in a 7-character plate drops the
score visibly, but a strong reading on the rest still indicates a high-
quality OCR.

A future enhancement (Phase 4.3 fusion engine) will use **per-character
confidence** rather than the mean to gate ambiguous chars (e.g.
`O` vs `0`) against the allow-list, but the mean is the right v1.

#### Dictionary contract

`dictionary_path` points to a UTF-8 text file with one character per
line. The model output's class 0 is the CTC blank token; class `i+1`
maps to dictionary line `i`. The recognizer enforces this on load —
if the model's class count doesn't equal `dictionary.size() + 1`, it
throws immediately with a descriptive `TrtException` so the failure
mode is "won't start" rather than "OCRs garbage".

The committed dictionary is tuned for **Kenya plates** specifically —
the format is `KXX 000X` (three letters + three digits + one letter),
and the 36-character vocabulary (0–9, A–Z) keeps the classifier head
small. Multi-region deployments swap the dictionary file without a
recompile; `server/models/README.md` documents the convention.

#### Allocation discipline

Identical to the detector: two host scratch buffers (`input_chw_`,
`output_logits_`), sized once at `load()` from the engine's resolved
shapes and reused for every recognition call. Per-frame inference
allocates only OpenCV's working memory for the resize + split.

#### Verified compile

```
[1/7] Scanning .../paddle_ocr_recognizer.cpp for CXX dependencies
[2/7] Generating CXX dyndep file
[3/5] Building CXX object .../paddle_ocr_recognizer.cpp.o
[4/5] Linking CXX static library libgate_inference.a
```

Built against the same toolchain as 4.2.1 and 4.2.2 (TensorRT 10.16.1,
CUDA 13.1.115, OpenCV 4.14.0, GCC 14.2.0,
`-std=c++20 -Wall -Wextra -Wpedantic -Werror`) — no diagnostics.

---

## Phase 4.2.4 — ALPR pipeline orchestrator

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/alpr_pipeline.hpp`](server/inference/include/inference/alpr_pipeline.hpp)
> and [`server/inference/src/alpr_pipeline.cpp`](server/inference/src/alpr_pipeline.cpp).

### What I built

`AlprPipeline` is the single class the rest of the server (gate
controller, fusion engine, gRPC service) talks to for license-plate
reads. It owns both inference backends from the previous milestones,
chains them, and exposes one method:

```cpp
std::vector<PlateReading> AlprPipeline::process(const cv::Mat& frame_bgr);
```

`PlateReading` carries the detector geometry **and** the OCR result
together, so a downstream consumer never has to correlate two
parallel arrays:

```cpp
struct PlateReading {
    cv::Rect2f  box;             // detector box in original-frame pixels
    std::string text;            // CTC-decoded plate string
    float       detection_score; // YOLO confidence
    float       ocr_score;       // mean per-char CTC confidence
};
```

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/alpr_pipeline.hpp` | Public API: `PlateReading`, `AlprPipeline::Config` (bundles detector + recognizer config + crop policy), move-only pipeline class. |
| `server/inference/src/alpr_pipeline.cpp` | Implementation: top-K cap, margin-expanded crop with frame-edge clamp, batched OCR, joined output assembly. |
| `server/inference/CMakeLists.txt` (updated) | Adds `alpr_pipeline.cpp` to the `gate_inference` static library. |

### Technical detail

#### Why one class instead of free functions

Detection and recognition share three traits that argue for a single
owning object: (1) both hold non-trivial scratch buffers that should be
allocated once at startup; (2) both wrap a `TrtEngine` whose CUDA stream
should not outlive the engine; (3) the *policy* knobs (margin, top-K,
OCR floor) need to live somewhere that isn't either backend. A pipeline
class concentrates ownership in one place — when the gate controller
constructs an `AlprPipeline`, two TensorRT engines and their CUDA
streams come up as a unit, and the destruction order at shutdown is
guaranteed correct because both backends are members.

The class is move-only and noexcept-movable for the same reason as the
backends: the eventual multi-camera setup will hold a
`std::vector<AlprPipeline>` (one per camera lane), and reallocation must
not run an engine destructor by accident.

#### Top-K crop policy

`detector_.detect()` returns boxes sorted by descending confidence.
The pipeline takes the top
`Config::max_plates_per_frame` (default 8) and discards the rest before
OCR is invoked. The cap exists for two reasons:

- **Latency protection on noisy frames.** EfficientNMS still emits up
  to `max_detections` boxes (typically 100). On a clean residential gate
  frame that's almost always 1 plate, but on a wide-angle parking-lot
  shot the recognizer would otherwise be invoked dozens of times for
  low-confidence noise.
- **Predictable upper bound on OCR latency.** Phase 4.3 (fusion engine)
  will run on a fixed frame budget; pinning the OCR fan-out makes that
  budget computable.

#### Crop with margin and clamp

CRNN models are trained on plates with a small border of background
context. Cropping flush to the YOLO box trims the leftmost/rightmost
character — a known failure mode that drops one or two characters from
the OCR output. `expand_and_clamp_()` pads each detector box by
`Config::crop_margin` (default 8 %) of its width/height on every side,
then clamps the resulting ROI to the frame so a plate detected at the
edge of view doesn't index outside the image.

The expansion is done in float (`cv::Rect2f` arithmetic), then quantized
once at the end — `floor` for the origin and `ceil` for the size — so a
fractional 0.5 px never costs a column. Boxes that clamp to fewer than
4×4 pixels are dropped before OCR is called; the recognizer would
reject the empty crop anyway and this keeps the failure local.

#### Batched OCR

The `recognize_batch` overload added in 4.2.3 isn't yet truly batched at
the TensorRT level — it loops the single-image path. The pipeline still
calls it (instead of looping itself) because that's the API surface
that **will** become batched in Phase 4.3 without changing the
pipeline. When the recognizer's enqueue path grows real batch support,
the pipeline gets the speedup for free and `process()` stays unchanged.

#### Output policy

OCR results are returned **unfiltered** by `ocr_score` — the
`Config::ocr_confidence_floor` field stores the threshold but
`process()` does not apply it. This is deliberate: low-confidence reads
are useful telemetry (a guard reviewing the dashboard wants to see the
"almost-recognized" plates) and the gate-control policy is the
authoritative consumer of the threshold. Encoding the policy at the
pipeline boundary would force the dashboard to either re-derive it or
read filtered data.

The result list is in detection-score order (inherited from
`YoloPlateDetector::detect`), so callers can take `result[0]` as "the
most likely primary plate in this frame" without resorting.

#### Allocation discipline

`process()` itself allocates exactly two `std::vector`s per frame
(`crops` and `kept`), each pre-reserved to `n_keep`. The `cv::Mat`
crops use OpenCV's reference-counted pixel data — `frame_bgr(roi)` is
an O(1) view; the explicit `.clone()` produces an independent buffer
the recognizer can safely consume. No CUDA allocations happen here;
both engines' device buffers were sized at `load()` time and are reused.

#### Verified compile

```
[1/7] Scanning .../alpr_pipeline.cpp for CXX dependencies
[2/7] Generating CXX dyndep file
[3/4] Building CXX object .../alpr_pipeline.cpp.o
[4/4] Linking CXX static library libgate_inference.a
```

Built against TensorRT 10.16.1, CUDA 13.1.115, OpenCV 4.14.0, GCC
14.2.0 with `-std=c++20 -Wall -Wextra -Wpedantic -Werror` — no
diagnostics. clang-format pass applied to match the project's
`.clang-format` (Google base, `ColumnLimit 100`, `IndentWidth 4`,
`IncludeBlocks Regroup`) so the CI Lint job stays green.

---

## Phase 4.2.5 — ONNX → TensorRT engine tooling

> **Completed 2026-04-25.** Scripts in
> [`scripts/export-models/`](scripts/export-models/) — `convert_onnx_to_trt.py`,
> `inspect_engine.py`, plus a [README](scripts/export-models/README.md)
> documenting the upstream YOLOv9 / PaddleOCR export commands.

### What I built

Phase 4.2.1–4.2.4 produced a C++ inference layer that consumes serialized
TensorRT engines (`.plan` files). 4.2.5 is the missing piece on the
**Python side**: how those `.plan` files are produced from upstream ONNX
exports, with the dynamic shape profiles, FP16 flags, and hardware
compatibility settings the C++ side expects to find at runtime.

| Artifact | Purpose |
|---|---|
| `scripts/export-models/convert_onnx_to_trt.py` | ONNX → `.plan` builder using TensorRT's Python `Builder` / `OnnxParser` / `BuilderConfig` API. FP16 default, optional INT8, `--min/--opt/--max-shape` profile knobs, `--hardware-compat ampere_plus` default for sm_80…sm_120 portability. Prints final I/O signatures after build. |
| `scripts/export-models/inspect_engine.py` | Deserializes a `.plan` and prints every I/O tensor's name, shape, dtype, and per-profile shape ranges. The fast way to verify the C++ Config tensor names still match what the engine actually exposes. |
| `scripts/export-models/README.md` | Documents the upstream export commands for YOLOv9 (`export.py --include onnx_end2end`) and PaddleOCR (`paddle2onnx`), plus the exact `convert_onnx_to_trt.py` invocations that match the C++ Config defaults. |

### Technical detail

#### Why a thin wrapper instead of `trtexec`

NVIDIA ships `trtexec` for one-off engine builds, and it's fine for that.
The trouble is that the engines this project produces aren't one-off:

- The PP-OCRv4 recognizer needs a **dynamic-shape optimization profile**
  with three width values (`min=32, opt=160, max=320`) chosen so the
  builder picks kernels tuned for typical Kenya plates (≈ 1:3.3 aspect)
  rather than for the worst-case 320 px. Encoding that profile in a
  shell-script `trtexec` invocation is doable but unreadable.
- Both engines need `HardwareCompatibilityLevel.AMPERE_PLUS` so the same
  `.plan` runs on the dev box (RTX 5060, sm_120) and the production
  server (RTX 4060, sm_89). `trtexec` exposes the flag but `--help` is
  ~400 lines and the right combination is not obvious.
- The C++ side binds tensors **by name**. Verifying that `softmax_2.tmp_0`
  is still what PaddleOCR exports needs an inspection step that's not a
  shell pipeline of `trtexec --dumpProfile` parsing.

A small, readable Python script that calls the same TRT API the C++ side
uses internally is easier to maintain than a shell wrapper around an
opaque tool.

#### `convert_onnx_to_trt.py` — what it actually does

Six steps:

1. **Logger.** A `trt.Logger` at `WARNING` (or `INFO` with `-v`) so the
   build output is small enough to read but loud enough to surface ONNX
   parse errors.
2. **Parse.** `Builder.create_network(0)` (explicit batch is now the only
   mode in TRT 10) → `OnnxParser`. On parse failure every parser error
   is dumped before exit so an upstream change to the ONNX surface
   doesn't fail silently.
3. **Builder config.** `set_memory_pool_limit(WORKSPACE, 4 GiB)` (CLI
   override available) — the workspace pool is what the builder uses to
   try alternative kernels, so a stingy budget produces measurably worse
   engines. 4 GiB fits both 8 GB GPUs with room to spare.
4. **Precision flags.** `BuilderFlag.FP16` by default. `INT8` is
   reachable via `--int8` but the project doesn't ship a calibration
   cache yet, so it'll fall back to FP16 for unquantized layers — the
   flag is there for Phase 5 hardening.
5. **Hardware compatibility.** `HardwareCompatibilityLevel.AMPERE_PLUS`
   trades 5–10 % inference speed vs. arch-specific kernels for a single
   `.plan` that runs on every machine in the fleet. Override with
   `--hardware-compat none` for benchmark-grade builds.
6. **Optimization profile.** If any of `--min/--opt/--max-shape` is
   given, all three are required and `--input-name` must point to the
   tensor. The shape parser accepts `NxCxHxW`-style strings so the CLI
   stays terse: `--max-shape 1x3x48x320`.

After `build_serialized_network()` returns the bytes, the script
re-deserializes the engine just to dump its I/O signatures — same code
path as `inspect_engine.py`. This confirms the build at the CLI without
needing a follow-up command.

#### `inspect_engine.py` — why a separate script

Engines outlive the build process. Six months from now, somebody
upgrades PaddleOCR, re-exports the recognizer, and the OCR result silently
drops one character because the output tensor was renamed from
`softmax_2.tmp_0` to `softmax_2`. The C++ side will throw a
`TrtException("tensor not found")` at startup, which is the right
behavior — but `inspect_engine.py` lets the operator see that mismatch
in seconds without booting the server, just by pointing it at the new
`.plan`.

It also prints the per-profile `(min, opt, max)` shape ranges for
dynamic inputs, which is the only way to confirm a converted engine's
profile actually matches what the C++ recognizer was sized for.

#### Why the upstream export commands are documented but not scripted

Both YOLOv9 and PaddleOCR ship their own canonical exporters as part of
their training repos. Mirroring those commands into a local Python
script would (a) immediately drift the moment upstream bumps a flag,
and (b) drag every dependency of `yolov9` and `paddleocr` into this
project's environment for a step that runs once. The README lists the
exact upstream commands instead, with the parameter values that produce
ONNX matching the C++ side's expectations (e.g. `--topk-all 100` so
EfficientNMS_TRT emits the `[1, 100, 4]` `det_boxes` shape that the
detector's `max_detections_` was sized for).

This is the same pattern the project uses elsewhere — never hand-write
canonical text; reference the canonical source.

#### What's *not* in this milestone

- Engine artifacts (`.plan` files). They're per-host, depend on the
  exact TRT version + GPU arch, and are gitignored under
  `server/models/`. Operator builds them at deploy time from the
  upstream ONNX.
- INT8 calibration cache. The flag is wired but the calibration data
  pipeline is a Phase 5 concern.
- Multi-batch optimization profiles. The C++ side runs one camera
  frame at a time; multi-image batching is a Phase 4.3.x feature and
  this script will gain a `--max-batch` flag when that lands.

---

## Phase 4.2.6 — Catch2 inference unit tests

> **Completed 2026-04-25.** Tests in
> [`tests/inference/cpu_algorithms_test.cpp`](tests/inference/cpu_algorithms_test.cpp).
> Algorithm extraction in
> [`server/inference/include/inference/detail/cpu_algorithms.hpp`](server/inference/include/inference/detail/cpu_algorithms.hpp)
> and [`server/inference/src/detail/cpu_algorithms.cpp`](server/inference/src/detail/cpu_algorithms.cpp).

### What I built

Eleven Catch2 v3 unit tests over the host-side inference algorithms — the
parts that don't need a GPU and therefore don't need TensorRT. They run in
under 0.3 s on a development workstation and exercise the same code path
the production server runs through, because the tests link against the
production `gate_inference` static library.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/detail/cpu_algorithms.hpp` | New `gate::inference::detail` namespace exposing the four pure-CPU primitives — `letterbox`, `unmap_letterbox_box`, `ctc_greedy_decode`, `expand_box_with_margin` — as free functions over `cv::Mat` / `std::span<const float>`. |
| `server/inference/src/detail/cpu_algorithms.cpp` | Implementation of those four functions, lifted out of the existing class methods. |
| `server/inference/src/{yolo_plate_detector,paddle_ocr_recognizer,alpr_pipeline}.cpp` | Refactored to delegate to the new free functions, eliminating duplicate implementations. |
| `tests/inference/cpu_algorithms_test.cpp` | 11 Catch2 test cases covering letterbox geometry, CTC blank/repeat collapse, confidence-mean math, and crop-with-margin clamping. |
| `tests/inference/CMakeLists.txt` | Builds `test_inference_cpu_algorithms`; gated on `TARGET gate_inference` so a CPU-only or proto-only build skips the inference tests cleanly. |
| `tests/CMakeLists.txt` (updated) | Same `if(TARGET …)` gate now applied to `proto/` so a tests-only build with `BUILD_PROTO=OFF` doesn't try to compile against `gate_proto`. |
| Root `CMakeLists.txt` (updated) | Reordered to process `server/` before `tests/`, so the `if(TARGET gate_inference)` gate sees the freshly-declared library. |

### Technical detail

#### Why a refactor was the right move

The pre-test code carried three copies of essentially the same algorithms,
each baked into a private member function:

- `YoloPlateDetector::letterbox_` had its own letterbox math.
- `YoloPlateDetector::detect()` did box-unmapping inline at the call site.
- `AlprPipeline::expand_and_clamp_` had its own crop expansion + clamp.
- `PaddleOcrRecognizer::ctc_decode_` had its own CTC decoder.

Testing any of them required either a `friend` declaration (an
implementation-detail leak that lives in the public header), a synthetic
TensorRT engine (slow, fragile across TRT versions), or constructing a
full backend instance (impossible without an engine file). None of those
are good answers. Lifting the algorithms into a `detail` namespace is the
*least* invasive option that makes them testable: the public headers don't
change, every backend now calls a single canonical implementation, and the
tests exercise that implementation directly without booting CUDA.

The classes still own the orchestration — buffer sizing, device transfer,
TensorRT enqueue/sync, ownership lifetimes. Only the pure-CPU bits moved.

#### What the tests actually cover

Eleven cases across three algorithms:

**Letterbox (`[letterbox]` tag)** — 4 tests:

1. *Horizontal source pads top and bottom.* A 200×100 source into a
   320×320 canvas. Verifies the chosen scale is the smaller axis ratio
   (1.6), the resulting padding is zero on the X axis and 80 px on the Y
   axis, the padding pixel matches the requested fill (114, 114, 114),
   and the centered image content is intact.
2. *Vertical source pads left and right.* The mirror case — a 100×200
   source into the same canvas, verifying scale and pads swap roles.
3. *Center box round-trips through scale and pad.* Constructs a known
   `LetterboxParams`, picks a canvas-pixel box in the image band, and
   confirms `unmap_letterbox_box` returns the source-pixel rectangle
   that letterbox would map back to. This is the contract the YOLO
   detector depends on.
4. *Clamps to source extents and drops degenerate.* Two sub-cases —
   a box that overhangs the canvas's image band clamps to the source
   bottom; a box entirely inside the top padding strip is degenerate
   after clamping and the function returns a zero-area rect.

**CTC greedy decode (`[ctc]` tag)** — 4 tests, each with a synthetic
argmax-probability tensor built by a test helper:

1. *Skips blanks and collapses repeats.* The sequence
   `blank, A, A, blank, B, B, C, blank` with peak prob 0.9 decodes to
   `"ABC"` with mean confidence 0.9. This is the canonical CTC
   correctness check.
2. *All blanks.* Every step is the blank token. Output is empty text
   and confidence exactly 0 (not NaN — important because downstream
   policy code compares against thresholds).
3. *Confidence is mean, not sum.* Three unique chars with peak prob
   0.5 yields mean 0.5, not 1.5. Catches a regression where the
   accumulator gets returned without dividing.
4. *Out-of-range class indices.* If the argmax picks a class beyond
   the dictionary, the kept-count still increments but the text
   doesn't grow — confirms the bounds check on `dictionary_[best - 1]`.

**Crop with margin (`[crop]` tag)** — 3 tests:

1. *10 % margin grows symmetrically.* A 100×50 box at (100, 100) with
   margin 0.1 grows to (90, 95) origin and (120, 60) extent.
2. *Clamps to frame on every edge.* A box near (0, 0) with a 50 %
   margin can't go negative; both `x` and `y` clamp to 0 and the
   right/bottom stay inside the frame.
3. *Degenerate result reports empty.* A 2×2 box with `min_extent=4`
   returns a 0-area rect, signalling the pipeline to drop this
   detection rather than feed an empty crop to OCR.

#### CMake gating model

The Catch2 inference tests live in `tests/inference/`. They link against
`gate_inference`, which only exists when the build is configured with
`BUILD_SERVER=ON` and a working CUDA + TensorRT toolchain. Forcing the
test target to always build would make the CI matrix
(`-DENABLE_GPU=OFF`) fail on a missing target.

The fix is a per-target gate in `tests/CMakeLists.txt`:

```cmake
if(TARGET gate_inference)
    add_subdirectory(inference)
endif()
```

This needs root `CMakeLists.txt` to process `server/` *before* `tests/`,
so the target is declared by the time the gate is checked. Same gate now
applies to `proto/` so a test-only build with `BUILD_PROTO=OFF` doesn't
try to compile against `gate_proto`. CI happily skips both subdirs and
runs only what it has dependencies for; local development builds get the
full test suite without changing flags.

#### Verified run

```
Test project /tmp/gate-build-inference
   1: letterbox: vertical source pads left and right ........... Passed
   2: letterbox: horizontal source pads top and bottom ......... Passed
   3: expand_box_with_margin: 10% margin grows symmetrically ... Passed
   4: ctc_greedy_decode: all blanks → empty + zero conf ........ Passed
   5: expand_box_with_margin: clamps to frame on every edge .... Passed
   6: unmap_letterbox_box: clamps + drops degenerate ........... Passed
   7: ctc_greedy_decode: skips blanks and collapses repeats .... Passed
   8: unmap_letterbox_box: center box round-trips .............. Passed
   9: ctc_greedy_decode: confidence is mean of kept probs ...... Passed
  10: expand_box_with_margin: degenerate reported as empty ..... Passed
  11: ctc_greedy_decode: out-of-range class indices dropped .... Passed

100% tests passed, 0 tests failed out of 11
```

Total runtime under 0.3 s on the dev box. The tests are fast enough to be
part of every local rebuild, not just CI gating.

---

## Phase 4.3.1 — Allowlist + blocklist store (SQLite)

> **Completed 2026-04-25.** Library in
> [`server/auth/`](server/auth/), tests in
> [`tests/auth/allowlist_store_test.cpp`](tests/auth/allowlist_store_test.cpp).

### What I built

The first non-inference library on the server side: `gate_auth` — a thin,
thread-safe SQLite wrapper that owns the allowlist (which plates are
permitted) and the blocklist (which plates are explicitly denied), keyed
per-site for multi-tenant deployments. The fusion engine in 4.3.2 reads
through this store; the gRPC `AdminService` in 4.3.4 will write to it.

| Artifact | Purpose |
|---|---|
| `server/auth/include/auth/allowlist_store.hpp` | Public API: `AllowlistStore::open(path)`, `upsert`, `lookup`, `is_blocklisted`, `blocklist_upsert`/`remove`, `list` (paginated), `remove`. Also `normalize_plate()` and the `is_allowed_now(entry, class, now_unix, now_local)` policy helper. |
| `server/auth/src/allowlist_store.cpp` | sqlite3 C-API implementation with prepared-statement caching, WAL journaling, and a versioned schema migration on open. |
| `server/auth/CMakeLists.txt` | Builds `gate_auth` (static library) linked against `gate_proto` (public, for the entry types) and `SQLite::SQLite3` (private). |
| `tests/auth/allowlist_store_test.cpp` | 12 Catch2 v3 cases — 58 assertions — exercising normalization, CRUD round-trips, cascade-on-remove, pagination ordering, blocklist site-scoping, and every `is_allowed_now()` decision branch. |
| `tests/auth/CMakeLists.txt` | Test binary `test_auth_allowlist_store`, gated on `TARGET gate_auth`. |
| `server/CMakeLists.txt`, `tests/CMakeLists.txt` (updated) | Same `if(TARGET …)` gating model as 4.2 — auth is added to server/ when `gate_proto` exists, and tests are added when `gate_auth` exists. |

### Technical detail

#### Why SQLite, why now

The architecture (ADR-002 and the proto schema) treats the allowlist as
a hot-path lookup against camera frame rate (~25 fps per gate). Three
options were on the table:

1. **In-memory hash-map.** Fast, trivial. Loses everything on restart;
   needs a separate persistence path (and reconciliation when the
   dashboard writes mid-flight). Rejected — the allowlist *is* state.
2. **A real RDBMS (PostgreSQL).** Overkill for a single-server
   deployment with thousands of rows; adds a daemon to operate.
3. **SQLite, embedded.** Single-file, transactional, ACID, WAL for
   concurrent dashboard writes during inference reads, no daemon.
   Already in `vcpkg.json`.

SQLite (#3) wins on every axis that matters: the working set is small
enough that the entire database fits in OS page cache, point lookups
through the `(site_id, plate_text)` primary key are O(log n) on a
B-tree at minimum, and the schema is simple enough that operators can
inspect it with `sqlite3 alphabet.db ".schema"`. The write rate is
human-scale (admin UI, occasional bulk imports) so WAL contention isn't
a concern.

#### Schema layout

Three normalized tables with cascading deletes:

- **`allowlist`** — `(site_id, plate_text)` primary key, plus owner
  metadata (`owner_name`, `owner_unit`), validity window
  (`valid_from`, `valid_until` as unix epoch seconds; 0 = unbounded),
  free-form `notes`, and an audit trail (`added_by`, `added_ts`).
  `WITHOUT ROWID` because the natural primary key is already small
  text and we never need a stable rowid.
- **`allowlist_classes`** — many-to-many, `(site_id, plate_text,
  vehicle_class)` triple. An empty join is interpreted as "any
  class allowed," matching the proto's documented semantics.
- **`allowlist_windows`** — many-to-many, `(start_minute_of_day,
  end_minute_of_day, days_of_week_mask)` per row. No primary key
  because duplicate windows are merely redundant, not invalid; an
  index on `(site_id, plate_text)` covers the lookup path.
- **`blocklist`** — separate table, `(site_id, plate_text)` primary
  key, `reason` for audit. Always wins over the allowlist per the
  fusion-engine verdict ladder.
- **`schema_version`** — one row, monotonically increasing. The
  current schema is v1; future migrations append a per-version
  upgrade block rather than writing speculative down-migrations.

`PRAGMA journal_mode = WAL` and `PRAGMA synchronous = NORMAL` give us
concurrent reader/writer access and crash-consistent durability without
the per-transaction fsync cost of `synchronous=FULL`. These are the
standard production defaults for SQLite as a service-of-record.

#### Plate-text normalization

The proto contract states stored plates are *normalized uppercase*. To
keep that invariant honest, every public API path runs the input
through `normalize_plate()`:

- ASCII lowercase → uppercase (no locale dependency, no `std::toupper`
  surprises with `setlocale`).
- Strip ASCII spaces, tabs, dashes, and underscores — the typical OCR
  noise from PaddleOCR's CTC head and from human-entered admin lists.
- Pass non-ASCII bytes through unchanged so internationalized plates
  (e.g. with Greek or Cyrillic glyphs) round-trip safely.

This is a free function deliberately — the dashboard backend will need
the same normalization before it queries the store, and exposing it in
the public header lets that code share the canonical implementation
rather than reinvent it.

#### `is_allowed_now()` — the policy helper

Returns `true` when an entry is *currently* valid for a given vehicle
class. Three independent gates, evaluated in cheapness order:

1. **Validity-window dates.** `now_unix < valid_from` or
   `now_unix >= valid_until` ⇒ deny. The half-open `[from, until)`
   interpretation matches how digital ACLs always work — an entry
   that "expires Friday at midnight" is denied at the first second of
   Saturday, not seconds before.
2. **Vehicle class.** If `allowed_classes` is empty, any class is
   accepted; otherwise the LiDAR-reported class must be on the list.
3. **Time-of-day window.** If no windows are set, always accept.
   Otherwise the *current* local time (passed in as `std::tm` by the
   caller — the policy helper is pure and TZ-agnostic) must satisfy
   one of the entry's `TimeWindow`s. Each `TimeWindow` carries a
   `days_of_week_mask` (bit0 = Mon … bit6 = Sun, per the proto) and a
   half-open `[start, end)` minute range. Wrap-around windows
   (`end < start`, e.g. 22:00–02:00) are encoded by inversion.

Splitting this out as a free function — rather than a method on
`AllowlistStore` — means the fusion engine can call it on the entry
returned by `lookup()` without re-querying the database, and tests
can synthesize entries inline without touching a SQLite handle.

#### Upsert semantics

The proto's `UpsertAllowlistRequest` is *batch-with-conflict-replace*:
the dashboard sends a list of entries, each is added if new and
updated if its `(site_id, plate_text)` already exists, and the
response reports inserted vs updated counts. The store implements that
literally:

- A single `BEGIN IMMEDIATE` / `COMMIT` per batch — either every
  entry lands or none do, with `ROLLBACK` on any per-row failure.
  That matters for bulk imports (e.g. an HOA importing 200 plates
  from a CSV) where partial commits are worse than retrying.
- The main-row write is a SQL `INSERT ... ON CONFLICT DO UPDATE`
  (the SQLite-flavored equivalent of `MERGE`), which is one round-trip
  to the engine instead of `SELECT then INSERT/UPDATE`.
- Sub-rows (`allowlist_classes`, `allowlist_windows`) follow a
  *wipe-and-rewrite* policy: delete all sub-rows for the key, then
  insert the new ones. This gives the upsert "set semantics" — the
  caller's submitted list of classes/windows is now exactly what's
  stored, with no ghost rows from prior versions.
- Inserted-vs-updated counting is via a precursor `SELECT 1` keyed on
  the same primary key. SQLite's `last_insert_rowid()` doesn't
  distinguish insert-from-update for `WITHOUT ROWID` tables.

#### Pagination

`list()` orders by `plate_text` and uses the last plate of the page as
the next page token — a classic keyset cursor. Two reasons over
LIMIT/OFFSET:

- O(log n) per page regardless of how deep the cursor walks. OFFSET
  on a 100k-entry site reading page 50 of 50 would scan 49 × page_size
  rows just to discard them.
- Stable under concurrent writes: a row inserted "above" the cursor
  doesn't shift the page boundary.

The implementation requests `limit + 1` rows per page — if the extra
row arrives, there's another page and the token is set; otherwise the
response is the last page. Page-size is clamped to `[1, 1000]`.

To avoid the obvious 2×N round-trips for hydrating sub-rows on a page,
`list()` issues exactly two follow-up queries with `IN (?, ?, …)`
clauses sized to the page width, then stitches results back into the
returned `Allowlistentry` objects in memory.

#### Threading model

A `std::mutex` serializes every public method against the underlying
`sqlite3*` handle. SQLite's own thread safety is `SQLITE_OPEN_FULLMUTEX`
on open (chosen here for clarity over `SQLITE_OPEN_NOMUTEX` + an
external mutex), but the in-process mutex still matters for
multi-statement operations (e.g. `BEGIN`/per-row `INSERT`/`COMMIT`)
where ordering across threads must be preserved. Lock contention is a
non-issue at the rates we expect — the camera produces a frame every
40 ms; the lookup path holds the lock for microseconds.

#### Move-only with PIMPL

`AllowlistStore` is move-only with a `std::unique_ptr<Impl>` for the
sqlite3 handle. PIMPL was deliberate: it keeps `<sqlite3.h>` out of the
public header, so downstream targets (fusion engine, gRPC services,
tests) don't transitively pull in the SQLite C API. The static
factory `open()` is the only constructor — there is no default
constructor, so a half-initialized `AllowlistStore` is unrepresentable.

#### Verified run

```
$ ninja gate_auth test_auth_allowlist_store
[7/7] Linking CXX executable tests/auth/test_auth_allowlist_store

$ ./tests/auth/test_auth_allowlist_store --reporter compact
RNG seed: 3014959169
All tests passed (58 assertions in 12 test cases)
```

All 12 cases run against `:memory:` databases — no filesystem state
leaks, no per-test cleanup, sub-millisecond per case. The full suite
(11 inference + 12 auth = 23 cases, 96 assertions) completes in well
under a second.

---

## Phase 4.3.2 — Fusion engine (verdict ladder)

> **Completed 2026-04-26.** Library in
> [`server/fusion/`](server/fusion/), tests in
> [`tests/fusion/fusion_engine_test.cpp`](tests/fusion/fusion_engine_test.cpp).

### What I built

The decision brain of the server: `gate_fusion` consumes an
`AuthorizeRequest` (a `DetectionFrame` from the ALPR + LiDAR pipelines
plus the site context) and returns an `AuthDecision` — the verdict the
firmware acts on and the dashboard logs. The verdict ladder follows
[`docs/diagrams/04-fusion-decision.mmd`](docs/diagrams/04-fusion-decision.mmd)
literally; nothing about the rules lives anywhere else in the codebase.

| Artifact | Purpose |
|---|---|
| `server/fusion/include/fusion/fusion_engine.hpp` | Public API: `FusionEngine{store, cfg}`, `decide(req)` (system-time path), `decide_at(req, now_unix, now_local)` (deterministic path), `set_override(gate_id, state)` for guard latches, plus a free `generate_uuidv4()` for decision IDs. |
| `server/fusion/src/fusion_engine.cpp` | Implementation: ten-step verdict ladder, multi-plate top-confidence picker, per-gate override map under a mutex, hand-rolled RFC 4122 UUIDv4 (no extra dep). |
| `server/fusion/CMakeLists.txt` | `gate_fusion` static library; public deps on `gate_proto` and `gate_auth`, no TensorRT/CUDA/OpenCV linkage. |
| `tests/fusion/fusion_engine_test.cpp` | 15 Catch2 cases / 148 assertions covering each branch of the ladder, the UUIDv4 format, and the multi-plate selection rule. |
| `tests/fusion/CMakeLists.txt` | `test_fusion_engine` binary, gated on `TARGET gate_fusion`. |
| `server/CMakeLists.txt`, `tests/CMakeLists.txt` (updated) | Same `if(TARGET …)` pattern as `auth/` — fusion is added when `gate_auth` exists, tests are added when `gate_fusion` exists. |

### Technical detail

#### Why a separate library, not part of `gate_auth`

The two are functionally orthogonal: `gate_auth` is a *data store*
(plates in / plates out, plus a pure validity helper), `gate_fusion` is
a *policy engine* (combines two probabilistic signals, applies overrides,
disambiguates failure modes). Splitting them means:

- The gRPC `AdminService` (4.3.4) can link only `gate_auth` — it has no
  business reading fusion config or override state.
- The `FusionEngine` can be unit-tested with an in-memory store and
  hand-built `AuthorizeRequest`s, without ever touching the dashboard
  service or the gRPC layer.
- A future replacement of the verdict ladder (e.g. a learned policy)
  swaps `gate_fusion` without disturbing the data layer or the API
  surface.

#### The verdict ladder, exactly

The ten steps run in this order; the first one to fire short-circuits.
The order matters — different orderings change semantics.

1. **Force-close override** → `DENIED (FORCE_CLOSE)`. A guard's
   lockdown beats every data-layer signal, including a guard's own
   later force-open if they conflict.
2. **No plate detected** → `DENIED (NO_PLATE_FOUND)`. Frame had a
   vehicle but no readable plate — surface as a distinct reason so
   the dashboard can show "ALPR retry needed."
3. **Pick best plate** by `detection_conf`. Multi-plate frames (rare
   on residential gates, common on parking-lot wide-angles) collapse
   to a single candidate here. Normalize via
   `gate::auth::normalize_plate()` and record on the decision.
4. **Pick best vehicle** by `class_conf`, default to
   `VEHICLE_CLASS_UNKNOWN` and the configured
   `lidar_missing_confidence` (default 0) when no vehicle was
   classified. Always record `matched_class`.
5. **Compute combined confidence** as `w1 * mean(detection, ocr) +
   w2 * lidar_class_conf`. The ALPR side is the *mean* of detection
   and OCR — a strong YOLO box around an unreadable plate is a poor
   match, and so is a clean OCR over a low-confidence detection.
6. **Force-open override** → `AUTHORIZED ("force-open override active")`.
   Runs *after* the plate is matched so the audit log records what was
   under the camera, not just "the guard latched the gate."
7. **Confidence floor** → `LOW_CONFIDENCE` with the more informative
   sub-reason: whichever side dragged the score down. A reason text
   includes the actual numbers (`combined=0.625, alpr=0.30, lidar=0.95,
   threshold=0.70`) so a guard reading the dashboard knows whether to
   reposition the camera or service the LiDAR.
8. **Blocklist** → `DENIED (BLOCKLISTED)`. Wins over the allowlist by
   construction; even a `override_allowlist=true` guard request cannot
   defeat a blocklist hit. (Force-open *can*, deliberately — see
   below.)
9. **Guard manual override (`override_allowlist`)** → `AUTHORIZED
   ("guard manual override")`. Bypasses the allowlist lookup but not
   the blocklist. Used when a visitor calls the intercom and the guard
   approves them through.
10. **Allowlist lookup**. If miss → `DENIED (NOT_ON_ALLOWLIST)`.
11. **Time-window + class restriction**. If
    `gate::auth::is_allowed_now()` denies, disambiguate the failure:
    - Wrong class → `MANUAL_REVIEW (CLASS_MISMATCH)`. Surfaces in the
      dashboard as "guard please decide" — a delivery van arriving at
      a sedan-only entry might still be legitimate.
    - Outside time window → `DENIED (OUTSIDE_WINDOW)`. Hard deny;
      time-windows are typically a hard contract (e.g. cleaning crew
      is *only* allowed Tuesday mornings).
12. Otherwise → `AUTHORIZED ("plate + class match")`.

Why force-open *can* defeat the blocklist (step 1 ahead of step 8) but
guard-`override_allowlist=true` *cannot* (step 9 after step 8):

- Force-open is a physical gate state ("gate stays up until I unlatch
  it") — a guard standing at the gate has already made the decision
  with full visual context. Pretending the data store still has a veto
  would be confusing operational behavior.
- `override_allowlist=true` is a *per-request* approval from the
  dashboard, often issued without seeing the vehicle. Letting a
  blocklisted plate through on a per-request approval is exactly the
  scenario blocklists exist to prevent.

#### Per-gate override state

Overrides are kept in an in-memory `std::unordered_map<gate_id,
OverrideState>` under a `std::mutex`. Three reasons not to persist them:

- They reflect **physical gate state**, which the firmware reports
  via `Telemetry`. The fusion engine's view should follow firmware,
  not lead it; persisting here would create a divergence risk.
- The dashboard is the source of truth for *requested* state. On
  server restart, the dashboard re-issues the latch.
- A locked-down gate after a multi-day server outage is almost never
  what operators want — re-confirmation by a human is better than a
  silent re-latch from a stale row.

The mutex is held for microseconds — the contention story is identical
to `AllowlistStore`'s. `set_override(gate_id, OverrideState::kNone)`
erases the entry rather than storing the kNone state, keeping the map
small.

#### Decision IDs — UUIDv4 by hand

Every `AuthDecision` carries a `decision_id` (per the proto), which the
dashboard uses to deduplicate replays and the firmware echoes on its
ack. The proto comment specifies UUIDv4. Rather than pull in a UUID
library, the engine generates them with `std::mt19937` seeded once per
thread from `std::random_device`, then sets the version (top nibble of
byte 6 = `0x4`) and variant (top two bits of byte 8 = `10`) bits per
RFC 4122. 122 bits of entropy per ID is plenty for a deployment that
issues at most a few decisions per second.

The implementation lives in the same TU as the engine; a unit test
matches 50 generated IDs against the canonical regex
`[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}`.

#### Two `decide` paths — production and deterministic

The public API exposes both `decide(req)` and `decide_at(req, now_unix,
now_local)`. The first delegates to the second after filling
`std::time(nullptr)` and `localtime_r(...)`. Tests always use the
deterministic path with hand-built `std::tm` for the day-of-week and
minute-of-day fields. This is the same pattern the inference layer
uses — pure code receives time as a parameter; the system-time
convenience wrapper lives at the API edge.

#### Verified run

```
$ ninja gate_fusion test_fusion_engine
[20/20] Linking CXX executable tests/fusion/test_fusion_engine

$ ./tests/fusion/test_fusion_engine --reporter compact
RNG seed: 2651663747
All tests passed (148 assertions in 15 test cases)
```

Combined with the prior milestones, the server-side test suite is now
38 cases / 244 assertions, all running under one second. Every prior
test still passes — the fusion library extends rather than replaces.

---

## Phase 4.3.3 — Dashboard event broadcaster

> **Completed 2026-04-26.** Library in
> [`server/dash/`](server/dash/), tests in
> [`tests/dash/event_broadcaster_test.cpp`](tests/dash/event_broadcaster_test.cpp).

### What I built

`gate_dash` — the server-side fan-out hub that the gRPC
`DashboardService::Subscribe` RPC will stream from. Every interesting
event the server produces (an `AuthDecision`, a firmware `Telemetry`
beat, a `FaultEvent`, an `OtaProgress` beat, a guard-issued
`GateCommand`, a `CommandAck`) is published here, and any number of
dashboard subscribers receive their own filtered, ordered stream.

| Artifact | Purpose |
|---|---|
| `server/dash/include/dash/event_broadcaster.hpp` | Public API: `EventBroadcaster{ring_capacity, per_sub_capacity}`, six typed `publish_*` builders, `subscribe(DashboardSubscription)` returning a move-only `Subscription` handle with `next(timeout)` / `drain_now()` / `close()`, plus `stop()`, `subscriber_count()`, `last_event_id()`, and a free `filter_matches()` helper. |
| `server/dash/src/event_broadcaster.cpp` | Implementation: monotonic per-server-boot `event_id` counter, two-level queueing (broadcaster ring buffer + per-subscriber bounded queue), drop-oldest back-pressure, pthread-backed condition variables, `weak_ptr`-based subscriber tracking with auto-reaping. |
| `server/dash/CMakeLists.txt` | `gate_dash` (static), depends on `gate_proto` and `Threads::Threads`. No TensorRT / CUDA / OpenCV / SQLite linkage. |
| `tests/dash/event_broadcaster_test.cpp` | 15 Catch2 cases / 56 assertions covering id assignment, every filter dimension (site, gate, kind, replay), ring eviction, per-sub drop-oldest, `stop()` waking blocked waiters, post-stop publish/subscribe rejection, and dead-subscriber reaping. |
| `tests/dash/CMakeLists.txt` | `test_dash_event_broadcaster` binary, gated on `TARGET gate_dash`. |
| `server/CMakeLists.txt`, `tests/CMakeLists.txt` (updated) | `dash/` added when `gate_proto` exists; tests added when `gate_dash` exists. |

### Technical detail

#### Why not just std::queue<DashboardEvent>

The dashboard subscription contract has three properties that a single
queue can't satisfy together:

1. **Replay-since-event-id.** A reconnecting dashboard sends its last
   seen `event_id`; the broadcaster has to ship the gap before the
   live stream starts. That requires keeping recent history *separate
   from* live delivery state.
2. **Per-subscriber filters.** Every subscriber wants its own slice
   (this site, those gates, only decisions and faults). Filtering at
   delivery time means each subscriber sees only what matches.
3. **Slow consumers must not block fast publishers.** A dashboard tab
   in a backgrounded browser must not delay the next `AuthDecision`
   from reaching firmware.

Two-level queueing solves all three:

- **Ring buffer (broadcaster level)** stores the last *N* events
  (default 4096) for replay. When `subscribe()` is called with a
  `since_event_id`, the broadcaster drains matching events from the
  ring into the new subscriber's queue before any live events flow.
  Old events fall off the back when the ring is full — the dashboard
  won't be able to replay arbitrarily far back, which matches the
  "best-effort live view" semantic.
- **Per-subscriber queue** (default 1024 events) is the staging area
  the subscriber's `next()` call drains. When that queue would
  overflow, the broadcaster drops the *oldest* event in *that
  queue only* and increments the subscriber's `dropped_events()`
  counter. The publisher is never blocked.

#### Filter rules — what made it into the proto and what didn't

The proto's `DashboardSubscription` has four `include_*` flags
(`decisions`, `telemetry`, `faults`, `ota`) but no flags for
`GateCommand` or `CommandAck`. Two reasonable interpretations:

- "If it's not in the proto, drop it." Simple, but means a
  decisions-only subscription wouldn't see a guard manually opening
  the gate — surprising and wrong.
- "Commands and acks are guard actions; always show them when the
  kind filter is in effect." This is what the implementation does;
  the rationale is documented in `filter_matches()`. A
  fully-defaulted subscription (all flags false) is treated as
  "everything" so a freshly-instantiated client doesn't silently
  drop all events.

`filter_matches()` is exposed from the header so the gRPC handler
(4.3.4) and the integration tests (4.3.6) can apply the same rule
without duplicating logic.

#### Where each event's gate_id comes from

The proto-level types each carry a `gate_id` field except
`OtaProgress` (keyed by `command_id`) and `CommandAck` (keyed by
`command_id`). The `publish_*` builders extract `gate_id` from the
payload where it exists and require the caller to supply it where it
doesn't. This keeps filter routing data in one place — the
`EventEnvelope` stored in the ring — without the broadcaster having
to know about every payload's schema.

#### Lifecycle and threading

`Subscription` is move-only and holds a `shared_ptr<Subscription::Impl>`.
The broadcaster keeps `weak_ptr` references in a `std::list` and
prunes dead ones on every `publish()`. That gives clean
"client-disconnected" semantics:

- The gRPC handler holds a `unique_ptr<Subscription>` for the life of
  the server-streaming RPC.
- When the client disconnects, the handler returns; the
  `unique_ptr` destructor calls `close()` which sets the closed flag
  and notifies the per-sub CV.
- The next `publish()` sees an expired `weak_ptr` and erases it from
  the broadcaster's list — no explicit unsubscribe needed.

`stop()` is the explicit shutdown path: it flips an atomic, takes the
broadcaster lock once to snapshot all live subscribers (lifting them
to `shared_ptr`), then walks the snapshot outside the lock to set
each subscriber's closed flag and notify. Holding the broadcaster
lock across the per-subscriber `notify_all()` would risk inverting
lock order with subscribers iterating their own queue.

#### What `drain_now()` is for vs `next()`

The gRPC streaming handler will mostly call `next()` with a 1-second
timeout (so it can periodically check whether the gRPC writer has
been cancelled). On replay though — right after `subscribe()` — the
handler can call `drain_now()` to scoop up whatever the broadcaster
already pre-loaded into the queue, write them to the stream, then
fall into the `next()` loop. That keeps the streaming start-up fast
without burning a wait on each replay event.

#### Verified run

```
$ ninja gate_dash test_dash_event_broadcaster
[10/10] Linking CXX executable tests/dash/test_dash_event_broadcaster

$ ./tests/dash/test_dash_event_broadcaster
Randomness seeded to: 787514901
===============================================================================
All tests passed (56 assertions in 15 test cases)
```

The full server-side suite is now 53 cases / 300 assertions across
inference, auth, fusion, and dash — under one second on a development
workstation. No prior tests regressed.

---

## Phase 4.3.4 — gRPC server + Dashboard/Admin services

> **Completed 2026-04-26.** Library in
> [`server/rpc/`](server/rpc/), tests in
> [`tests/rpc/`](tests/rpc/).

### What I built

The first piece of the server that speaks to the outside world: a
`grpc::Server` lifecycle wrapper plus two of the three services from
the `gate.v1` proto contract — `DashboardService` (for the web UI
backend) and `AdminService` (for allowlist CRUD). The third service,
`FieldControllerService`, requires a bidi stream and a stitch-up to
the ALPR pipeline; it lands in 4.3.5 alongside `main.cpp`.

| Artifact | Purpose |
|---|---|
| `server/rpc/include/rpc/server.hpp` + `src/server.cpp` | `gate::rpc::Server` — owns `grpc::Server`, registers services, exposes `start()` / `wait()` / `shutdown(deadline)` / `bound_address()`. Built with health-check enabled and `InsecureServerCredentials`; TLS is a deployment concern handled in 4.8. |
| `server/rpc/include/rpc/admin_service.hpp` + `src/admin_service.cpp` | `AdminServiceImpl` — three unary RPCs (`UpsertAllowlist`, `ListAllowlist`, `DeleteAllowlist`) that adapt protobuf shapes to the `AllowlistStore` C++ API. Empty `site_id` → `INVALID_ARGUMENT`; store exceptions → `INTERNAL`. |
| `server/rpc/include/rpc/dashboard_service.hpp` + `src/dashboard_service.cpp` | `DashboardServiceImpl` — `Subscribe` (server-streaming → broadcaster), `IssueCommand` (publishes the command + a synthesized "received" ack to the bus), `Authorize` (delegates to `FusionEngine::decide` and publishes the result). |
| `server/rpc/CMakeLists.txt` | `gate_rpc` static library; public deps on `gate_proto + gate_auth + gate_fusion + gate_dash`. Carries no system or runtime deps of its own — gRPC and Threads come transitively via `gate_proto` and `gate_dash`. |
| `tests/rpc/admin_service_test.cpp` | 4 cases / 24 assertions: round-trip upsert→lookup→delete, paginated list, validation errors. |
| `tests/rpc/dashboard_service_test.cpp` | 4 cases / 25 assertions: `IssueCommand` stamps id+ts and publishes both command and ack to the bus, `Authorize` delegates to fusion and publishes the decision, validation errors on missing fields. |
| `tests/rpc/server_lifecycle_test.cpp` | 2 cases / 4 assertions: real `grpc::Server` binds an ephemeral port and reports the resolved address; binding port 1 unprivileged correctly fails. |
| `tests/rpc/CMakeLists.txt` | Three test binaries, all gated on `TARGET gate_rpc`. |
| `server/CMakeLists.txt`, `tests/CMakeLists.txt` (updated) | rpc subdir gated on `TARGET gate_fusion AND TARGET gate_dash`; tests on `TARGET gate_rpc`. |

### Technical detail

#### Why split AdminService into a thin proxy

`AdminServiceImpl` does almost nothing of its own — it copies entries
from the request's repeated field into a `std::vector`, calls
`AllowlistStore::upsert`, and copies the stats back. The temptation
was to skip the wrapper entirely and have the gRPC stub speak to the
store directly. Three reasons it's still worth its weight:

- **Validation lives at the gRPC boundary**, not in the data store.
  The store enforces invariants on what it stores; the service rejects
  malformed *requests* (empty `site_id`) with `INVALID_ARGUMENT`. That
  separation lets the store be reused by future non-gRPC callers
  (CLI bulk import, simulation harness) without re-deciding what
  "valid" means.
- **Status-code translation** is a service responsibility. The store
  throws `AuthDbException` on SQLite failures; the service catches
  and surfaces `INTERNAL`. Keeping that catch in the service means
  the store layer can change its exception type without leaking into
  every RPC handler.
- **Tests are the same shape as production**: the test calls
  `svc.UpsertAllowlist(&ctx, &req, &resp)` exactly as gRPC will. No
  mocks, no fakes, no glue.

The proto reuses `UpsertAllowlistResponse` for `DeleteAllowlist`'s
return — the count of removed rows lands in the `updated` field with
`inserted = 0`. That's documented in the impl comment so future
readers don't grep for a missing `DeleteAllowlistResponse` message.

#### IssueCommand: synthesized "received" ack now, real completion ack later

The dashboard's `IssueCommand` RPC is unary — it returns a single
`CommandAck`. But the *real* ack — the one that says "the firmware
actually pulsed the relay" — comes back via `FieldControllerService`
(which 4.3.5 delivers) and from there flows out the same dashboard
event bus to every other dashboard subscriber. So `IssueCommand`'s
direct response is necessarily synthetic: `received_ts` set to now,
`completed = false`, `success = false`. The dashboard knows to wait
for the *completion* ack via its `Subscribe` stream.

Two events go on the bus per `IssueCommand` call:

1. The command itself — so other dashboards see "guard:alice issued
   OPEN_GATE on gate-north." The command_id is filled in if the
   caller didn't supply one (most won't), via the shared
   `gate::fusion::generate_uuidv4()` from 4.3.2.
2. The synthesized "received" ack — so a dashboard that subscribed
   *before* issuing also gets the receipt-confirmation event,
   without having to special-case "I'm the one who issued this."

When the firmware later completes the command, 4.3.5's
`FieldControllerService` will publish the *completion* ack to the
same bus, with the same `command_id` and `completed=true`.

#### Subscribe: cancellation requires polling

gRPC's server-streaming `ServerWriter::Write()` returns `false` when
the client has gone away, but there's no "wake me when the writer
cancels" primitive that composes with our broadcaster's condition
variable. So the handler runs a poll loop:

- Drain any replayed events first (no wait — they were preloaded
  into the per-sub queue at `subscribe()` time).
- Loop on `sub->next(subscribe_poll_interval)`, default 500ms. On
  timeout, re-check `ctx->IsCancelled()` and `bus_.stopped()`.
- On a real event, call `writer->Write(ev)`; bail if it fails.

500ms is the right tradeoff for this product: a dashboard tab that
closes is detected within 500ms (well under typical user perception
of "instant"), while idle-bus CPU is one wakeup every 500ms per
subscriber — negligible.

#### The Server lifecycle

`grpc::Server` is built via `ServerBuilder` with health-check enabled
(satisfies the standard `grpc.health.v1.Health` service automatically;
operators get `grpc_health_probe`-friendly endpoints for free).
`AddListeningPort()` accepts a pointer-out parameter for the resolved
port; passing `:0` gives an ephemeral port and we mirror the resolved
address back via `bound_address()` for tests.

`shutdown()` is the graceful-stop path: it computes a deadline from
the configured `deadline` parameter (default 2 seconds) and calls
`grpc::Server::Shutdown(deadline)`. In-flight RPCs get the deadline to
finish; new RPCs are rejected immediately. The destructor calls
`shutdown()` if `start()` was ever called, so leaving scope is a clean
shutdown by default.

#### Dependency direction stays one-way

The CMake gate model now reads top-to-bottom: `gate_proto` →
`{gate_auth, gate_dash}` → `gate_fusion` (depends on auth + proto) →
`gate_rpc` (depends on all four). No cycle, no transitive reach
across the graph. This matters because the next milestone (4.3.5)
adds a thin `main.cpp` that links *only* `gate_rpc` plus
`gate_inference` for the ALPR pipeline — adding the field controller
service doesn't reshape the rest of the tree.

#### Verified run

```
$ ninja gate_rpc test_rpc_admin test_rpc_dashboard test_rpc_server
[25/25] Linking CXX executable tests/rpc/test_rpc_admin

$ ./tests/rpc/test_rpc_admin
All tests passed (24 assertions in 4 test cases)
$ ./tests/rpc/test_rpc_dashboard
All tests passed (25 assertions in 4 test cases)
$ ./tests/rpc/test_rpc_server
All tests passed (4 assertions in 2 test cases)
```

The full server-side suite is now **63 cases / 353 assertions** across
inference + auth + fusion + dash + rpc, all passing.

---

## Phase 4.3.5 — FieldControllerService + gate-server daemon

> **Completed 2026-04-26.** New service in
> [`server/rpc/{include/rpc,src}/field_controller_service.{hpp,cpp}`](server/rpc/),
> daemon entry point in [`server/src/main.cpp`](server/src/main.cpp).

### What I built

The third gRPC service from `gate.v1` — `FieldControllerService` — and
the daemon binary that brings everything online: `gate-server`. The
firmware can now connect (or rather, will connect once Phase 4.4 ships
firmware), the dashboard can subscribe and issue commands, the admin
can manage the allowlist, and a SIGTERM drains the whole stack
cleanly.

| Artifact | Purpose |
|---|---|
| `server/rpc/include/rpc/field_controller_service.hpp` + `src/field_controller_service.cpp` | `FieldControllerServiceImpl` — `Control` (bidi stream — Telemetry/Ack/Fault from firmware → bus, GateCommand from bus → firmware via a writer thread per connection), `SubmitDetection` (firmware-side ALPR shortcut: `DetectionFrame` → `FusionEngine::decide` → `AuthDecision`), `DeliverOta` and `ReportOtaProgress` returning `UNIMPLEMENTED` until Phase 4.5. |
| `server/src/main.cpp` | The composition root. CLI11 for arg parsing, spdlog for logging, opens the SQLite allowlist, constructs the fusion engine and broadcaster, registers the three services via `gate::rpc::Server`, installs `SIGINT`/`SIGTERM` handlers using async-signal-safe `sig_atomic_t` polling, drains the broadcaster + gRPC server on shutdown. |
| `server/CMakeLists.txt` (updated) | New `gate-server` executable target — builds only when `gate_rpc` is available. Links `gate_rpc + CLI11::CLI11 + spdlog::spdlog`. |
| `server/rpc/CMakeLists.txt`, `include/rpc/server.hpp`, `src/server.cpp` (updated) | Register the new service alongside Admin and Dashboard, mirror the configurable poll interval into the field controller. |
| `server/rpc/src/dashboard_service.cpp` (updated) | `IssueCommand` now wires `LATCH_OPEN` / `LATCH_CLOSE` / `RELEASE_LATCH` to `FusionEngine::set_override` *before* publishing the command — happens-before guarantee for races between issue+authorize. |
| `tests/rpc/field_controller_service_test.cpp` | 4 cases / 13 assertions: `SubmitDetection` round-trip, validation, both OTA stubs return `UNIMPLEMENTED`. |
| `tests/rpc/dashboard_service_test.cpp` (updated) | New regression case for the LATCH-override wiring (8 sub-assertions). |
| Daemon smoke verified: `gate-server --listen 127.0.0.1:0 --db-path /tmp/test.db`, then `kill -TERM` → exits 0 with "Shutdown requested — draining…" + "gate-server stopped." in the log. |

### Technical detail

#### Bidi Control stream — one writer thread per firmware connection

gRPC's synchronous server model gives each RPC its own thread. For
unidirectional RPCs that's fine, but bidi `Control` needs to read
from the firmware *and* write to it concurrently. The clean pattern
is one extra thread for the writer side:

- Read the first envelope (must be `Telemetry`) to learn the
  firmware's `gate_id`. Without that we don't know how to route
  commands; we'd be guessing every payload's owner.
- Subscribe to the broadcaster filtered to that gate, with the
  command kind bit set so non-command events drop out before the
  writer-thread check.
- Spawn a writer thread that polls `sub->next(poll_)`, drops
  non-Command events as a defensive layer, and writes `Command`
  envelopes to the stream until the client disconnects or the
  context is cancelled.
- Reader loop runs on the gRPC handler thread: dispatches each
  `Telemetry`/`CommandAck`/`FaultEvent` to the broadcaster.
  `Command` payloads from the firmware side are server-only and
  silently dropped.
- On reader exit (firmware disconnected), set the writer's
  shutdown flag and join.

Two threads per connection scales to dozens of gates per server —
plenty for the residential and small-commercial deployments this
project targets. A high-fanout site with hundreds of gates would
want async/callback-based gRPC; that's a future call out, not a
problem for v1.

#### Why Telemetry-first as the gate identifier

Every other payload type either has no `gate_id` (`OtaProgress`,
`CommandAck`) or could plausibly arrive *after* a reconnect with
stale state. Forcing the firmware to send `Telemetry` first means:

- The server learns the gate identity from a payload designed to
  carry it (the `Telemetry` message has `gate_id` as field 1).
- A reconnecting firmware re-identifies itself unambiguously.
- A misbehaving client that sends `FaultEvent` first gets an
  immediate `INVALID_ARGUMENT` rather than a fuzzy "no gate" error
  later.

The first telemetry is published to the broadcaster too, so the
dashboard sees the connection via the same event stream as later
beats.

#### Where LATCH commands change override state

A force-open or force-close override is *physical state* — the gate
stays open or stays closed until the guard releases the latch. Two
places could update `FusionEngine::set_override`:

1. `DashboardService::IssueCommand` (where the command is born).
2. `FieldControllerService::Control` writer (just before delivery to
   firmware).

This milestone wires it in (1). Reasoning:

- The override is an *immediate* effect of the dashboard's decision.
  A subsequent `Authorize` that races the publication should see
  the new state.
- It works even if no firmware is connected — useful for tests, dev
  mode, and during firmware reboot windows.
- Centralizing it at the point of authorization keeps the control
  flow legible: the dashboard writes the command, the engine state
  updates, the bus carries the command, the firmware (when
  connected) executes it.

A LATCH command with no firmware listener still updates fusion
state, which is the right behavior — a guard locking down at 2 AM
shouldn't depend on the gate's network being up.

#### Daemon lifecycle and signal handling

The earlier draft of `main.cpp` used `std::condition_variable::notify_all()`
inside a `std::signal` handler. That's undefined behavior — POSIX
permits only async-signal-safe operations from a true signal context,
and `pthread_cond_signal` (which `notify_all` ultimately calls) isn't
on the list. The fix is the canonical pattern:

```cpp
volatile std::sig_atomic_t g_shutdown_requested = 0;
void handle_signal(int) { g_shutdown_requested = 1; }
// In main:
while (g_shutdown_requested == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
}
```

Setting a `sig_atomic_t` is one of the very few operations the
standard guarantees safe from a signal handler. The 100 ms poll
adds at most one tenth of a second to shutdown latency — negligible
for a daemon, and the cost is purely on the shutdown path.

Verified clean exit on `SIGTERM`: process logs "Shutdown requested
— draining…", calls `bus.stop()` (wakes every `Subscribe` RPC),
calls `server.shutdown(deadline)` (drains in-flight RPCs),
returns 0 from main.

#### CLI surface

`gate-server --help` (rendered):

```
gate-automation server: gRPC + ALPR + LiDAR fusion daemon
OPTIONS:
  --listen TEXT [0.0.0.0:50051]
  --site-id TEXT [default]
  --db-path TEXT [allowlist.db]
  --log-level TEXT [info]
  --shutdown-deadline UINT [5]
```

CLI11's `default_str` puts the current default in square brackets
on the help page — important for ops engineers grepping
`--help` to figure out where the database lives.

#### Verified run

```
$ ninja gate-server test_rpc_admin test_rpc_dashboard test_rpc_field test_rpc_server
[31/31] Linking CXX executable server/gate-server

$ ./tests/rpc/test_rpc_admin
All tests passed (24 assertions in 4 test cases)
$ ./tests/rpc/test_rpc_dashboard
All tests passed (33 assertions in 5 test cases)
$ ./tests/rpc/test_rpc_field
All tests passed (13 assertions in 4 test cases)
$ ./tests/rpc/test_rpc_server
All tests passed (4 assertions in 2 test cases)

$ ./server/gate-server --listen 127.0.0.1:0 --db-path /tmp/x.db &
[info] gRPC server listening on 127.0.0.1:41037
$ kill -TERM $!; wait
[info] Shutdown requested — draining...
[info] gate-server stopped.
exit code: 0
```

The full server-side suite is now **67 cases / 374 assertions** across
inference + auth + fusion + dash + rpc + the new field service. The
server binary is ~7 MB stripped, links statically against everything
except the system runtime, and starts in under 5 ms.

---

## Phase 4.3 complete — end-to-end gRPC server delivered

> **Completed 2026-04-26.** Final sub-milestone (4.3.6) tests in
> [`tests/integration/grpc_roundtrip_test.cpp`](tests/integration/grpc_roundtrip_test.cpp).

### What I built

The capstone of Phase 4.3: end-to-end integration tests over real
gRPC channels, plus the closure sweep that retires Phase 4.3 in the
master timeline. Every layer of the server is now exercised both in
isolation (unit tests) and through the wire (integration tests).

| Artifact | Purpose |
|---|---|
| `tests/integration/grpc_roundtrip_test.cpp` | 7 cases / 55 assertions: a `ServerHarness` that spins up `gate::rpc::Server` on `127.0.0.1:0`, builds real client stubs, and runs the four RPC services through their paces — Admin CRUD over the wire, Dashboard `Authorize`, `IssueCommand`+`Subscribe` event-stream, FieldController `SubmitDetection`, and the bidi `Control` stream forwarding a command + accepting an ack. |
| `tests/integration/CMakeLists.txt` | `test_integration_grpc_roundtrip` binary, gated on `TARGET gate_rpc`, with a 30s `catch_discover_tests` timeout for the server-startup overhead. |
| `tests/CMakeLists.txt` (updated) | Adds the `integration/` subdir alongside `rpc/`. |
| README master timeline | Phase 4.3 → ✅ Complete; Phase 4.4 (Firmware drivers) → 🔵 In progress — next. |

### Technical detail

#### Why a separate integration tier — not just bigger unit tests

The handler-level tests in `tests/rpc/` exercise each service's
proto adapter and validation logic by calling the methods directly
with synthetic `ServerContext` objects. They run in microseconds and
form the bulk of test coverage. But they can't catch:

- **HTTP/2 framing and gRPC channel mechanics.** Did the
  `AddListeningPort` succeed? Does `BuildAndStart()` actually
  produce a server that accepts connections? Does the Channel
  resolve `127.0.0.1:0` to the right port?
- **The bidi `Control` stream.** The handler-level test for
  `Control` would need a fake `ServerReaderWriter`, which is
  awkward and error-prone — the real one has subtle blocking
  semantics that affect cancellation.
- **Server-streaming `Subscribe`.** Same issue: the writer's
  cancellation behavior under client disconnect is a property of
  the gRPC layer, not the handler code.

The integration tier is the only place those cross-layer behaviors
live. It runs after every other tier passes — if a unit test fails,
that's where the bug is; if an integration test fails on a passing
unit suite, the wire layer is the suspect.

#### `ServerHarness` — one server per test, ephemeral port

Each `TEST_CASE` constructs its own `ServerHarness`:

```cpp
class ServerHarness {
    AllowlistStore store_ = AllowlistStore::open(":memory:");
    FusionEngine fusion_{store_};
    EventBroadcaster bus_;
    Server server_{store_, fusion_, bus_, makeConfig()};
    std::shared_ptr<grpc::Channel> channel_;
public:
    ServerHarness() {
        REQUIRE(server_.start());
        channel_ = grpc::CreateChannel(server_.bound_address(),
                                       grpc::InsecureChannelCredentials());
    }
    ~ServerHarness() {
        bus_.stop();
        server_.shutdown(std::chrono::milliseconds{500});
    }
};
```

Two design choices worth flagging:

- **`127.0.0.1:0` ephemeral port.** Lets cases run in parallel
  without port collisions. The harness reads
  `server_.bound_address()` after `start()` — that field carries
  the resolved port, exactly the pattern the lifecycle test in
  4.3.4 already exercised.
- **`bus_.stop()` *before* `server_.shutdown()`** in the destructor.
  If we shut down the server first, in-flight `Subscribe` RPCs
  would block on `next()` until their poll fired — `bus_.stop()`
  flips the closed flag and the next `next()` returns `nullopt`,
  letting the handler exit immediately. Cleanest teardown order.

#### The bidi `Control` test — the one that proves it all works

This is the test the whole architecture had to be designed to
make possible:

1. Open `Control(ctx)`. The client gets a `ClientReaderWriter`.
2. Client writes `Telemetry{gate_id="gate-north"}` to identify
   itself.
3. Test sleeps 100 ms (so the server's writer thread starts up
   and the broadcaster subscription is registered before the
   next event), then publishes a `GateCommand` directly to the
   bus targeted at `gate-north`.
4. Client reads the next envelope from the stream — must be the
   forwarded command.
5. Client writes a `CommandAck{completed=true}` back to the
   server.
6. Test starts a separate `Subscribe` RPC and waits for the ack
   to appear on the dashboard event stream — proving the reader
   side of `Control` correctly published the ack via the bus.

If this case passes, the full server-side data plane works:
firmware → server reads, server bus fan-out, server writes →
firmware. Failure modes for this test are mostly thread-ordering
issues; the 100 ms grace is the safety margin against subscription
registration races.

#### The complete server-side test surface

| Tier | Binary | Cases | Assertions |
|---|---|---|---|
| Inference (CPU algos) | `test_inference_cpu_algorithms` | 11 | 38 |
| Auth (allowlist store) | `test_auth_allowlist_store` | 12 | 58 |
| Fusion (verdict ladder) | `test_fusion_engine` | 15 | 148 |
| Dash (event broadcaster) | `test_dash_event_broadcaster` | 15 | 56 |
| RPC handlers — Admin | `test_rpc_admin` | 4 | 24 |
| RPC handlers — Dashboard | `test_rpc_dashboard` | 5 | 33 |
| RPC handlers — Field | `test_rpc_field` | 4 | 13 |
| RPC server lifecycle | `test_rpc_server` | 2 | 4 |
| Integration (gRPC wire) | `test_integration_grpc_roundtrip` | 7 | 55 |
| **Total** | | **75** | **429** |

The whole suite runs in well under 5 seconds on a development
workstation. Every layer below the gRPC wire is reachable from
production code paths, so the unit tests cover what production
runs; the integration tier confirms the wire layer agrees.

#### What Phase 4.3 delivered

The user-visible artifact is one binary — `gate-server` — that
exposes a complete `gate.v1` gRPC surface:

- **AdminService** — allowlist CRUD with CRUD + paginated list.
- **DashboardService** — `Authorize` (run a frame through the
  fusion engine and get a verdict), `IssueCommand` (latch / pulse
  / open / close, with side effects on fusion override state),
  `Subscribe` (stream all events with site/gate/kind filters and
  replay-since-event-id).
- **FieldControllerService** — `Control` bidi stream for
  firmware ↔ server, `SubmitDetection` for dev-mode ALPR
  shortcut, `DeliverOta` and `ReportOtaProgress` stubbed for
  Phase 4.5.

The binary takes a SQLite path, an address to bind, and a site id;
it logs via spdlog and shuts down cleanly on `SIGTERM`. The full
data-plane works end-to-end: a dashboard can issue a command, the
firmware (when Phase 4.4 lands) will receive it via `Control`,
ack it, and that ack flows back out the dashboard's `Subscribe`
stream — all without any code in `main.cpp` knowing about the
specifics of either side.

#### What Phase 4.4 will need

The next milestone (firmware drivers) will need to consume this
gRPC surface from the ESP-IDF side. The contract is fixed in
`shared/proto/gate_service.proto`; the wire is verified by these
integration tests. Phase 4.4's job is the W5500 ethernet driver,
the relay-control GPIOs, the limit-switch and safety-beam GPIOs,
the LED strip driver, and the small Telemetry beat that opens
`Control`. The fusion engine's overrides will start lighting up
gates the moment Phase 4.4 lands.

---

## Phase 4.4.1 — ESP-IDF firmware skeleton + hello-world build

> **Completed 2026-04-26.** Project tree under
> [`firmware/`](firmware/), build instructions in
> [`firmware/README.md`](firmware/README.md).

### What I built

The bedrock for the gate-side firmware: a real ESP-IDF v6.x project
that builds clean for the ESP32-S3 target, with a 4 MB OTA-capable
partition layout, a `gate_drivers` component skeleton waiting for
the GPIO/W5500/safety/LED drivers in subsequent sub-milestones, and
a `main` entry point that logs a boot banner and idles. The earlier
empty-stub `firmware/{app,drivers,include,platform,src,tests}/` tree
was a placeholder layout from Phase 0; it didn't fit ESP-IDF's
component model and held no actual code, so it was replaced.

| Artifact | Purpose |
|---|---|
| `firmware/CMakeLists.txt` | Top-level project file. Calls into `$IDF_PATH/tools/cmake/project.cmake` so `idf.py build` finds the standard component registration hook. |
| `firmware/sdkconfig.defaults` | Compile-time pinning: target ESP32-S3, FreeRTOS @ 1 kHz, `-O2`, exceptions+RTTI off (saves ~40 KB), task watchdog @ 10 s, custom partition table, 4 MB / 80 MHz flash. |
| `firmware/partitions.csv` | Two-OTA 4 MB layout: `nvs`(16K) + `otadata`(8K) + `phy_init`(4K) + `ota_0`(1.5M) + `ota_1`(1.5M) + `storage`(960K SPIFFS for offline allowlist cache). |
| `firmware/main/CMakeLists.txt` + `main.cpp` | App entry component. `app_main()` logs a boot banner with version + IDF revision + chip features, then idles in a 1-second `vTaskDelay` loop. Idle becomes the gate state-machine task in Phase 4.5. |
| `firmware/components/gate_drivers/` | First-party hardware-driver component. 4.4.1 ships only a `version.{hpp,cpp}` so `main` has something to depend on; subsequent milestones add `relay`, `limit_switch`, `w5500`, `safety_beam`, `led`. |
| `firmware/README.md` | Quick-start: install ESP-IDF, source `export.sh`, `idf.py set-target esp32s3`, `idf.py build`, expected first-boot console output. |
| `.github/workflows/lint.yml` (updated) | clang-tidy step now scopes to `server/ shared/ dashboard/backend/` — running host clang-tidy on xtensa-targeting firmware would produce bogus errors. clang-format still covers firmware for stylistic checks. Firmware-specific static analysis lands with the firmware CI workflow in 4.4.5. |

### Technical detail

#### Why a fresh tree, not retrofit the old stubs

The pre-existing `firmware/` tree had `app/`, `drivers/`, `include/`,
`platform/`, `src/`, `tests/` — a layout that mirrors the *server*
side. That doesn't match ESP-IDF's expectations:

- ESP-IDF's CMake project hook auto-discovers components from
  `main/` (the application entry) and `components/` (extra custom
  components). `drivers/` and `app/` aren't recognized.
- `idf_component_register()` is the unit of registration —
  per-component CMakeLists declaring sources, includes, and
  dependencies. The old stubs had none.

I deleted the empty stubs (each was 1-2 lines of comment header,
no actual code) and rebuilt as a proper ESP-IDF tree. Future
sub-milestones grow `components/gate_drivers/` rather than scattering
files across the previous folder layout.

#### Why two OTA slots and not factory + ota

ESP-IDF supports both layouts. "Factory + ota" gives you a
read-only fallback image; "ota_0 + ota_1" is the modern
ping-pong-update layout where every binary is upgradable. ADR-009
(OTA strategy) calls for ed25519-signed images that the firmware
verifies before swapping the active partition — that's
fundamentally a two-slot pattern. The partition table here matches
that contract: 1.5 MB per slot leaves comfortable room for the
LWIP + W5500 + esp-tls + grpc client stack the firmware will
eventually carry.

The first-boot bootloader picks `ota_0` automatically (no ota_data
populated yet). After the first successful OTA the ota_data
partition records the active slot; from there the bootloader
ping-pongs.

#### Compiler flags worth flagging

`CONFIG_COMPILER_CXX_EXCEPTIONS=n` and
`CONFIG_COMPILER_CXX_RTTI=n` together save ~40 KB of flash on a
hello-world image. The cost is no `try/catch` and no
`dynamic_cast` — both irrelevant for embedded code that has to
handle hardware errors via return codes anyway. If a future driver
needs RTTI for a CRTP-free polymorphic dispatch, we'll revisit, but
that's an unlikely requirement.

`CONFIG_COMPILER_OPTIMIZATION_PERF=y` (-O2) is the production
default; `-Og` for debug-build local development is one
`menuconfig` toggle away. Logging defaults to INFO level with
RTOS-tick timestamps, matching what the server-side spdlog produces
so cross-system logs line up.

#### One sub-milestone, three build artifacts

`idf.py build` produces:

- `build/bootloader/bootloader.bin` — 18.5 KB, the second-stage
  bootloader. Lives at flash offset `0x0`; reads the partition
  table and ota_data to pick which OTA slot to boot.
- `build/partition_table/partition-table.bin` — 3 KB, the
  partition table itself at offset `0x8000`.
- `build/gate_firmware.bin` — 152 KB, the actual app image. Lands
  at `0x10000` (the start of `ota_0`). 90 % of the 1.5 MB OTA slot
  is free — plenty of room for the network stack and gRPC client
  in upcoming sub-milestones.

These three plus `build/ota_data_initial.bin` (the empty-OTA-data
seed) are what `idf.py flash` writes to the chip.

#### One v6.1 quirk worth recording

ESP-IDF v6.1-dev renamed the chip-info struct's `full_revision`
field to `revision` (still encoded as `MXX`, with M = major and
XX = minor, just shorter). The boot banner uses
`chip.revision / 100` and `chip.revision % 100` for the
two-component display. v5.x users porting forward will hit the
same compile error.

#### What the lint workflow change covers

Pre-4.4.1, the `firmware/` tree held only header stubs with no real
code, so host `clang-tidy` happily processed them as empty
translation units. With actual ESP-IDF includes (`#include <esp_log.h>`)
in `main.cpp`, host clang-tidy can't resolve those headers — they
live under the xtensa toolchain include paths, not `/usr/include`.
The workflow update scopes clang-tidy to `server/ shared/
dashboard/backend/`, leaving clang-format (which doesn't need
preprocessor expansion) to keep covering firmware for style.

A dedicated firmware CI workflow comes in Phase 4.4.5 — that one
will install ESP-IDF, run `idf.py build`, and produce its own
firmware-targeting compile_commands.json for tools that need it.

#### Verified run

```
$ . ~/esp/esp-idf/export.sh
$ idf.py set-target esp32s3
$ idf.py build
…
Successfully created ESP32-S3 image.
Generated /home/deby/projects/gate-automation/firmware/build/gate_firmware.bin
gate_firmware.bin binary size 0x265c0 bytes. Smallest app partition is
  0x180000 bytes. 0x159a40 bytes (90%) free.
```

Three artifacts produced: `bootloader.bin` (18 KB),
`partition-table.bin` (3 KB), `gate_firmware.bin` (152 KB,
10 % of the 1.5 MB OTA slot). Phase 4.4.2 grows that with the
relay + limit-switch GPIO drivers; subsequent milestones add the
W5500 ethernet wrapper, safety-beam interrupt, and LED-pattern
renderer.

---

## Phase 4.4.2 — relay + limit-switch GPIO drivers

> **Completed 2026-04-26.** New components in
> [`firmware/components/gate_drivers/`](firmware/components/gate_drivers/).

### What I built

The first hardware-touching code in the firmware: two C++ classes that
sit on top of ESP-IDF's `esp_driver_gpio` and `esp_timer` and deliver
exactly the operations the proto's `CommandKind` enum implies.

| Artifact | Purpose |
|---|---|
| `gate_drivers/include/gate_drivers/relay.hpp` + `src/relay.cpp` | `Relay` — single-output relay driver with two operating modes: latched (`set(true)/set(false)`) for `LATCH_OPEN`/`LATCH_CLOSE` proto commands, and pulsed (`pulse(duration)`) for `PULSE_RELAY`/`OPEN_GATE`/`CLOSE_GATE`. `active_high=false` flips polarity for opto-isolated relay boards. Non-copyable, non-movable (esp_timer captures `this`). |
| `gate_drivers/include/gate_drivers/limit_switch.hpp` + `src/limit_switch.cpp` | `LimitSwitch` — debounced GPIO input. Polls at 200 Hz via `esp_timer`, commits state transitions only after 20 ms of stable readings (configurable). User callback fires from the esp_timer task — not an ISR — so it's safe to do real work. NO/NC polarity abstracted; `is_active()` always reads "true = limit reached". |
| `gate_drivers/CMakeLists.txt` (updated) | Adds `relay.cpp` + `limit_switch.cpp` to the source list and pulls in `esp_driver_gpio` + `log` as REQUIRES. (ESP-IDF v6 split the legacy `driver` component per-peripheral; the right name is `esp_driver_gpio` now.) |

### Technical detail

#### Why pulse() restarts the timer instead of stacking

A second `pulse(500ms)` while the first 200 ms pulse is still active
should make the line-on window 500 ms — not the union of the two
intervals, not the first interval ignored. The implementation
cancels the in-flight `esp_timer` and re-arms with the new duration:
"re-arm rather than stack." That matches how a guard rapidly
double-clicking the dashboard's "open gate" button intuitively
expects the relay to behave.

`set(false)` mid-pulse cancels the pulse cleanly via the same
`esp_timer_stop` call. No race window where the timer fires after
the explicit set.

#### Polling vs interrupt for limit switches

The naive design is "GPIO interrupt on edge → start a debounce
timer → if pin still in new state after `debounce_ms`, commit".
That works but makes the constructor leak ISR install state and
splits debounce logic across two callbacks. The polling design
trades one esp_timer wake every 5 ms for a single linear state
machine in `on_poll()`. At 200 Hz across two limit switches per
gate, the CPU cost is negligible (~2 µs per wake on an ESP32-S3).

The polling-based debounce uses three counters:

- `last_sample_` — the previous raw read. A change re-seeds the
  stability count.
- `stable_samples_` — consecutive ticks the new value has been
  stable. Cleared on a glitch.
- `samples_to_commit_` — derived once at construction from
  `debounce_ms / poll_period_ms`.

When `stable_samples_` crosses the threshold, the new state
commits via an atomic store and the user callback fires. The
callback runs in the esp_timer task context (not an ISR), so it
can take mutexes and call any ESP-IDF API safely.

#### NO vs NC polarity

`normally_open=true` (the default and most common configuration
for end-of-travel limits) means the switch contacts are open when
the limit is *not* reached; the firmware's input pin reads HIGH
through the internal pull-up. When the gate reaches the limit,
the switch closes to ground and the pin reads LOW. `read_active()`
abstracts this away so the rest of the codebase only ever asks
"is the limit reached?" without thinking about wiring polarity.

#### Verified compile

```
$ idf.py build
…
Successfully created ESP32-S3 image.
gate_firmware.bin binary size 0x265c0 bytes. Smallest app
  partition is 0x180000 bytes. 0x159a40 bytes (90%) free.
```

`relay.cpp.obj`, `limit_switch.cpp.obj`, and `version.cpp.obj` all
present in the gate_drivers component build directory. Same image
size as 4.4.1 because nothing in `main.cpp` references the new
classes yet — the linker correctly garbage-collects them. They get
pulled into the binary once Phase 4.5's state machine instantiates
real `Relay` and `LimitSwitch` objects per the gate_id config.

The drivers build clean under the project's `-Wall -Wextra
-Werror` and pass clang-format with the project's `.clang-format`
profile (Google base, ColumnLimit 100, IndentWidth 4).

#### What's intentionally not here

- **No ISR-based limit switch.** Discussed above — polling is
  sufficient for the rate and gives a single-callback contract.
- **No relay PWM.** The proto's commands are binary on/off; PWM
  for pre-actuator soft-start belongs in a future driver if any
  gate requires it.
- **No driver-level tests.** Hardware-touching code can't be
  unit-tested on host without a GPIO mock layer — that's a
  larger investment that doesn't pay off until we have multiple
  drivers worth abstracting. The compile under `-Werror` is the
  current safety net; on-hardware integration testing comes in
  Phase 4.9.

---

## Phase 4.4.3 — W5500 SPI-ethernet driver wrapper

> **Completed 2026-04-26.** New component header in
> [`gate_drivers/include/gate_drivers/ethernet.hpp`](firmware/components/gate_drivers/include/gate_drivers/ethernet.hpp),
> implementation in
> [`gate_drivers/src/ethernet.cpp`](firmware/components/gate_drivers/src/ethernet.cpp).

### What I built

The firmware can now talk IP. A thin C++ wrapper around the
`espressif/ethernet_init` managed component (which handles the W5500
PHY/MAC factory functions), `esp_netif`, and the IDF event loop.
Reduces the standard 14-step W5500 bring-up sequence to a one-line
`Ethernet::start({})` call plus three optional callbacks for link
state and DHCP IP events.

| Artifact | Purpose |
|---|---|
| `gate_drivers/include/gate_drivers/ethernet.hpp` | Public API: `Ethernet::start(Config)`, `on_link / on_got_ip / on_lost_ip` callback registration, `is_up()` + `current_ip()` snapshots. Process-wide singleton — ESP-IDF's event loop and netif are global. |
| `gate_drivers/src/ethernet.cpp` | Implementation: `esp_netif_init` + `esp_event_loop_create_default` once, `ethernet_init_all()` to spin up the W5500 from Kconfig pins, netif glue attach, MAC derivation from the chip factory ID (locally-administered bit set), event handler registration, `esp_eth_start`. Atomic state for lock-free `is_up`/`current_ip` reads from other tasks. Callbacks fire from the event loop task — not an ISR — so they're safe to do real work. |
| `gate_drivers/idf_component.yml` | New: declares `espressif/ethernet_init ^1.3.0` as a managed dependency. The component manager downloads this on first `idf.py reconfigure` and pulls in `espressif/w5500` transitively. |
| `gate_drivers/CMakeLists.txt` (updated) | Adds `ethernet.cpp` to sources; new REQUIRES: `esp_eth + esp_event + esp_netif + esp_hw_support + ethernet_init`. |
| `firmware/sdkconfig.defaults` (updated) | Pins the W5500 to the project's schematic GPIOs (MOSI=11, MISO=13, SCLK=12, CS=10, INT=9, RST=8), 20 MHz SPI clock, on `SPI2_HOST`. Configurable via `idf.py menuconfig` → *Example Ethernet Configuration*. |
| `firmware/main/main.cpp` (updated) | `app_main` now starts the driver after the boot banner and registers a `got_ip` log callback. The `link_up` event also logs. Phase 4.5 will use these signals to start the gRPC Control stream. |

### Technical detail

#### Why the `ethernet_init` managed component

ESP-IDF v6.x has split the chip-specific SPI ethernet drivers (W5500,
DM9051, ENC28J60, etc.) out of the main `esp_eth` component into
separately-versioned managed components. `espressif/ethernet_init`
is the umbrella that wraps every one of them behind a Kconfig
selector and a uniform `ethernet_init_all()` factory. Instead of
hand-coding `eth_w5500_config_t` + `eth_phy_config_t` plus the SPI
device init, the wrapper:

1. Reads `CONFIG_ETHERNET_SPI_DEV0_W5500=y` and the
   `CONFIG_EXAMPLE_ETH_SPI_*_GPIO` pins from sdkconfig.
2. Builds the right MAC + PHY factory chain.
3. Returns ready-to-attach `esp_eth_handle_t` instances.

That's the layer of detail this milestone shouldn't be touching.
Hardware revisions (e.g., switching to a DM9051 for a future board
spin) are a sdkconfig change, not a code rewrite.

#### Singleton — and why that's the right call here

`Ethernet` is a class with only static methods and a hidden
function-local `static State&` for storage. Three reasons it's a
singleton:

- `esp_netif_init` and `esp_event_loop_create_default` install
  process-wide state; calling them twice errors. Wrapping that as
  a constructor would force every caller to know about ordering.
- A typical gate board has *one* ethernet interface. Multi-port
  setups (e.g., a backup cellular link) would use a different
  netif type entirely.
- The IDF event loop callbacks are C function pointers with a
  `void*` arg — instance-per-handler is awkward. A singleton
  with a function-local State has zero ordering bugs and matches
  the IDF idiom.

If we ever need multi-instance support, refactoring is one
`static State*` per netif handle away.

#### MAC derivation

W5500 has no factory MAC — every board ships with the same
default. ESP-IDF's chip factory MAC (from efuse) is unique per
chip; flipping the locally-administered bit (0x02 on byte 0) gives
us a derived MAC that won't collide with any IEEE-assigned vendor
range. Callers can override by passing a non-zero `Config::mac`,
useful when a deployment wants a stable-across-reflashes identity.

#### Why DHCP first, static IP later

The proto's `Telemetry.gate_id` is the routing key the dashboard
uses to identify a gate; the network address is just transport.
For v1, DHCP-on-the-LAN is the simplest config — operators don't
have to coordinate IP allocation per gate, and the dashboard's
firmware-controller stream (`Control` RPC) handles reconnection
when the lease changes. A static-IP escape hatch is easy to add
later by exposing it through `Config::static_ip` and calling
`esp_netif_dhcpc_stop` + `esp_netif_set_ip_info` from `start()`.

#### Verified compile + image growth

```
$ idf.py build
…
Successfully created ESP32-S3 image.
gate_firmware.bin binary size 0x53000 bytes. Smallest app
  partition is 0x180000 bytes. 0x12d000 bytes (78%) free.
```

Image grew from 152 KB (4.4.1, hello-world) → 332 KB now that the
W5500 driver, lwIP stack, and DHCP client are actually linked into
the binary (since `app_main` calls `Ethernet::start()` directly).
Still 78 % of the 1.5 MB OTA slot free for the gRPC client and
state machine that land in 4.5.

#### What's intentionally not here

- **No actual hardware verification.** Real W5500 + ethernet
  cable + DHCP server is on-bench / on-deployment work. The
  compile + Kconfig wiring is the verification this milestone
  delivers; the on-hardware smoke test happens in Phase 4.9.
- **No TLS yet.** The proto's `gate_service.proto` carries a
  `bytes signature = 9` field on `GateCommand` for ed25519
  per-message signing, and ADR-009's OTA contract specifies
  signed images. Channel-level TLS for the gRPC Control stream
  is a 4.8 deployment concern (cert provisioning) — the
  `InsecureChannelCredentials` server-side default in 4.3.4 was
  picked to match.

---

## Phase 4.4.4 — safety-beam input + LED status driver

> **Completed 2026-04-26.** Two new drivers in
> [`gate_drivers/`](firmware/components/gate_drivers/):
> [`safety_beam.{hpp,cpp}`](firmware/components/gate_drivers/include/gate_drivers/safety_beam.hpp)
> and
> [`status_led.{hpp,cpp}`](firmware/components/gate_drivers/include/gate_drivers/status_led.hpp).

### What I built

The two remaining hardware drivers needed before the firmware app
(Phase 4.5) can wire up the gate state machine: a fast-debounce
beam-break input that triggers safety stops, and a three-color LED
pattern renderer that turns the proto's `LedPattern` enum into
animated GPIO toggles.

| Artifact | Purpose |
|---|---|
| `gate_drivers/include/gate_drivers/safety_beam.hpp` + `src/safety_beam.cpp` | `SafetyBeam` — same poll+debounce pattern as `LimitSwitch` but tuned for safety: 1 ms poll period, 5 ms debounce (worst-case ~6 ms detection latency). `is_blocked()` always reads "true = beam interrupted" regardless of electrical polarity. Failsafe-aligned default (active-high when blocked) — a disconnected sensor wire reads HIGH and looks blocked. |
| `gate_drivers/include/gate_drivers/status_led.hpp` + `src/status_led.cpp` | `StatusLed` — drives three discrete LEDs (red/green/yellow) and renders the proto's `LedPattern` enum as animated patterns: `kAuthFlash` (3× green), `kDenyFlash` (3× red), `kFaultSlow` (1 Hz red), `kOtaPulse` (1 Hz yellow), `kBootOk` (solid green 2 s), `kOff`. 100 ms tick periodic timer; `render(p)` is non-blocking and interrupts in-flight patterns cleanly. |
| `gate_drivers/CMakeLists.txt` (updated) | Adds `safety_beam.cpp` + `status_led.cpp` to the source list. No new REQUIRES — both drivers reuse `esp_driver_gpio + esp_timer + log`. |

### Technical detail

#### Why the safety beam is its own class instead of a re-tuned LimitSwitch

`LimitSwitch` and `SafetyBeam` both wrap the same primitive (poll a
GPIO with a debounce window) but they're semantically different
concerns:

- A **limit switch** says "we reached the end of travel." Stale
  signals are mostly harmless; a 50 ms debounce is fine.
- A **safety beam** says "stop the motor immediately, somebody is
  in the gate's path." Latency directly translates to risk; 5 ms
  is the right knob.

Naming the class `SafetyBeam` makes that intent explicit at the
call site. A future reader scanning `firmware/main/main.cpp` for
"how does the gate avoid crushing people" reads `SafetyBeam` and
knows where the safety logic lives. With a single shared class,
the same intent would be hidden inside a config struct.

The implementations are 90 % copy of `LimitSwitch` — that's fine.
The DRY win isn't worth the readability loss; if a future bug
fix needs to apply to both, grep handles it.

#### Failsafe wiring assumption

The default `active_high_when_blocked = true` matches the standard
"open-collector beam receiver with pull-up" wiring used by virtually
every commercial gate beam set:

- Beam clear → receiver pulls input LOW.
- Beam blocked or sensor unpowered/unplugged → input floats HIGH
  (via the internal pull-up) → reads as blocked → gate refuses to
  close.

This is the right failsafe direction: a fault makes the system
*more* cautious, never less. Hardware that wires the opposite
sense (a dedicated "fault output" that asserts when the sensor is
healthy) should override the flag — but that wiring is rare and
the override is one config field away.

#### LED pattern timing

100 ms tick was chosen because:

- `kAuthFlash` and `kDenyFlash` (3× flash) need to feel snappy
  but visible. 100 ms on / 100 ms off / repeat 3× = 600 ms total
  pattern. Faster reads as a single blink; slower drags out the
  feedback past the point where a guard at the gate notices.
- `kFaultSlow` and `kOtaPulse` need a 1 Hz cadence — that's 5
  ticks on, 5 ticks off, repeating, at 100 ms tick.
- `kBootOk` is "solid green for 2 s" — 20 ticks then off.

Picking a single tick rate that satisfies all six patterns means
one timer, one state machine, one place the LED logic lives. A
WS2812 strip (addressable RGB) would let us do gradients and
fades, but the schematic uses three discrete indicators which
keeps the BOM cheaper and the driver Stack-overflow-safe.

#### Render semantics — interrupt rather than queue

`render(Pattern::kDenyFlash)` mid-AUTH-flash immediately switches
to the deny pattern from frame 0. There's no queue of pending
animations. Reasoning:

- The dashboard is the source of truth for what the LED should
  show *right now*. A queued AUTH-then-DENY would mean "the
  guard pressed open then deny — finish the open animation
  first" — confusing operational behavior.
- The proto's `GateCommand` carries a `LedPattern led_pattern`
  field on the *command*, with no notion of pattern history.
  The implementation matches the contract.

The implementation uses an atomic `restart_requested_` flag set
by `render()` and consumed by the timer callback — so the next
tick after a render() call resets `step_ = 0` cleanly. No race
between render() and an in-flight tick.

#### Verified compile

```
$ idf.py build
…
Successfully created ESP32-S3 image.
gate_firmware.bin binary size 0x53000 bytes (332 KB), 78% of the
1.5 MB OTA slot free.
```

All six driver `.cpp.obj` files present:
`version`, `relay`, `limit_switch`, `safety_beam`, `status_led`,
`ethernet`. Image size unchanged from 4.4.3 because `safety_beam`
and `status_led` aren't yet referenced from `main.cpp` — same
linker-garbage-collection situation as the relay/limit_switch in
4.4.2. They get pulled into the binary in 4.5 when the state
machine instantiates them with site-specific pin configs.

All four new files compile clean under `-Wall -Wextra -Werror`
and pass clang-format with the project's `.clang-format` profile.

#### What's intentionally not here

- **No ISR-based beam input.** The polling design at 1 ms tick
  matches the safety latency requirement (~6 ms worst case)
  without ISR-safety constraints. Edge-triggered ISRs would be
  faster on paper but the gate motor inertia (100 ms+ to start
  reversing) absorbs the difference, and the simpler code is
  easier to audit for safety.
- **No PWM brightness control on the LEDs.** Three discrete
  on/off LEDs are sufficient for the six patterns the proto
  defines. PWM would require LEDC peripheral wiring and channel
  allocation — easy to add later if a richer dashboard demands
  it.

---

## Phase 4.4 complete — firmware CI + driver suite delivered (latest)

> **Completed 2026-05-09.** Final sub-milestone (4.4.5) workflow at
> [`.github/workflows/firmware.yml`](.github/workflows/firmware.yml).
> Final green commit: [`9f09ba9`](../../commit/9f09ba9).

### What I built

The capstone of Phase 4.4: a GitHub Actions workflow that builds the
firmware for the ESP32-S3 target on every push, plus the closure
sweep that retires Phase 4.4 in the master timeline. The five driver
classes built across this phase (`Relay`, `LimitSwitch`, `SafetyBeam`,
`StatusLed`, `Ethernet`) are now exercised by CI on every commit.

Getting the workflow green was a four-commit journey — the initial
workflow file (`bcf26f6`) failed three different ways before the
component manager, the IDF version, and the project's `sdkconfig`
all agreed on the same world. The "[Three CI fixes](#three-ci-fixes-that-landed-phase-44)"
section below captures each failure and the underlying cause.

| Artifact | Purpose |
|---|---|
| `.github/workflows/firmware.yml` | New workflow — uses Espressif's official `espressif/esp-idf-ci-action@v1` to install ESP-IDF v5.5 in a Docker image, fetch managed components (the W5500 driver via `ethernet_init`), and run `idf.py build` for the `esp32s3` target. Path-filtered to firmware/ changes so server-side commits don't burn the runner. |
| `firmware.yml` artifact upload | The workflow persists `bootloader.bin`, `partition-table.bin`, `gate_firmware.bin`, `gate_firmware.elf`, and `flash_args` for 14 days on each run, so a future release workflow can pull them without recompiling and a CI failure can be diagnosed without re-running. |
| `firmware/components/gate_drivers/idf_component.yml` | `ethernet_init` pinned to `==1.0.0` (last release before the 1.1.0+ Kconfig solver bug — see fix #2 below). Locked tight so a future patch-version bug can't slip past. |
| `firmware/sdkconfig.defaults` | SPI Kconfig prefix corrected from `EXAMPLE_ETH_SPI_*` (legacy example component) to `ETHERNET_SPI_*` (what `ethernet_init` v1.0.0 actually reads), plus the `CONFIG_ETHERNET_SPI_SUPPORT=y` master gate that was silently defaulting to `n` (see fix #3). |
| README master timeline | Phase 4.4 → ✅ Complete; Phase 4.5 (Firmware app — state machine, gRPC client, OTA) → 🔵 In progress — next. |

### Technical detail

#### Why a separate workflow, not a job in build.yml

`build.yml` is the **host** build matrix — gcc-13/14 + clang-17/18 ×
Debug/Release × `-DENABLE_GPU=OFF`. It builds `server/` and `tests/`
on the host architecture. Bolting an ESP-IDF build onto that matrix
would multiply the runner cost by 8× for no benefit; firmware
changes are uncorrelated with toolchain matrix coverage.

The new `firmware.yml` is single-job, single-target, scoped to
firmware/ paths. A change that doesn't touch firmware (which is
most server-side commits) skips the firmware runner entirely via
`paths` filtering. That keeps total CI time predictable as the
codebase grows.

#### ESP-IDF v5.5 vs the local v6.1-dev install

Local development uses the rolling `v6.1-dev` checkout because
that's what `~/esp/esp-idf` points at on the dev workstation. CI
pins to **v5.5** — what production deployments will be built
against, and the lowest stable version that satisfies the pinned
managed components.

The two versions agree on every API the firmware drivers use:

- `chip.revision` is present in both (v5.4+ added it as uint16
  alongside the older uint8 `revision`; v6.x removed
  `full_revision` so `revision` is the only field). The boot
  banner's `revision / 100` + `% 100` math works on both.
- `esp_driver_gpio` was added in v5.2 and is the canonical name
  in v5.5 + later. The legacy `driver` umbrella is still
  available as a fallback.
- `espressif/ethernet_init ==1.0.0` (idf >=5.4) is compatible
  with both. The `~1.3` constraint that ships in the action's
  template would have worked on idf >=5.4.3, but its Kconfig
  manifest crashes the version solver — see fix #2 below.

If a future API divergence between the two versions makes one
side unworkable, the action exposes `esp_idf_version: v6.0`
(or `v6.1` once released) as a one-line update.

<a id="three-ci-fixes-that-landed-phase-44"></a>
#### Three CI fixes that landed Phase 4.4

The initial Firmware workflow (`bcf26f6`) failed on its first run.
Each of the three follow-up commits surfaced a deeper layer of the
ESP-IDF build pipeline:

**Fix 1 — IDF version floor (`6c3df3e`).**
The first failure happened during the component manager's version
solve. `espressif/ethernet_init` v1.3.0 (the latest release on
2026-05-09) declares `idf_version: ">=5.4.3, !=5.5.0, !=5.5.1"`,
but the action's `release-v5.4` Docker tag tracks v5.4.0 — below
the floor. Querying Docker Hub confirmed that `release-v5.X.Y`
patch tags are not published; only minor-version `release-v5.4`
and `release-v5.5` exist. `release-v5.5` (last re-tagged
2026-04-07, after v5.5.4 was published 2026-03-27) sits inside
the satisfiable range, so bumping `esp_idf_version` from `v5.4` to
`v5.5` cleared the floor.

**Fix 2 — pin around the component-manager Kconfig bug (`73d0407`).**
With the version floor satisfied, the solver got further but
crashed with:

    idf_component_tools.errors.MissingKconfigError: ETHERNET_SPI_USE_CH390

`ethernet_init` v1.1.0 (2025-10-20) added optional transitive
dependencies on individual PHY components (`ch390`, `dm9051`,
`enc28j60`, …) gated by `if: $CONFIG{ETHERNET_SPI_USE_*}` clauses.
The component manager evaluates those clauses **during** version
solving — before any component's Kconfig has been registered with
the build system. Any reference to a not-yet-defined symbol
crashes the solver before cmake even starts.

This is upstream's bug, not ours. The workaround is to pin to the
last release before the regression: `==1.0.0` (2025-09-24), whose
manifest has zero `$CONFIG{...}` if-clauses — every dependency is
either unconditional or rules-based on `idf_version`/`target`.
v1.0.0 still supports W5500 over SPI on idf >=5.4 and is fully
compatible with the pin map this project uses.

**Fix 3 — Kconfig prefix mismatch + the silent master gate (`9f09ba9`).**
With v1.0.0 fetched and compiling, the build progressed deep into
the IDF tree (1060 / 1083 objects) before failing with:

    ethernet_init.c:728: error: array subscript i is outside array
    bounds of 'eth_device[0]' [-Werror=array-bounds=]
    note: while referencing 'eth_instance_g'
    static eth_device eth_instance_g[CONFIG_ETHERNET_INTERNAL_SUPPORT
                                   + CONFIG_ETHERNET_SPI_NUMBER];

Both Kconfigs evaluated to 0, so the static array was zero-length
and any indexed read tripped the compiler's bounds checker. The
`sdkconfig.defaults` had two problems:

1. **Missing master gate.** `CONFIG_ETHERNET_SPI_SUPPORT` defaults
   to `n`, and every `ETHERNET_SPI_*` sub-symbol lives inside
   `if ETHERNET_SPI_SUPPORT`. With the gate off, even
   `ETHERNET_SPI_DEV0_W5500=y` was inert and `ETHERNET_SPI_NUMBER`
   fell back to 0.
2. **Wrong prefix on the pin/host/clock symbols.**
   `ethernet_init` v1.0.0 reads `CONFIG_ETHERNET_SPI_HOST`,
   `CONFIG_ETHERNET_SPI_SCLK_GPIO`, `CONFIG_ETHERNET_SPI_MOSI_GPIO`,
   etc. The defaults file had the legacy
   `CONFIG_EXAMPLE_ETH_SPI_*` prefix from the older
   `example_eth_init` example component — those settings were
   silently ignored, leaving the pin map at IDF defaults instead
   of this project's W5500 schematic (SCLK=12 / MOSI=11 / MISO=13 /
   CS=10 / INT=9 / RST=8 on `SPI2_HOST` at 20 MHz).

The fix renames the prefixes and adds the master gate. After this,
the firmware workflow went green and all 14 CI checks landed
clean on `9f09ba9`.

The shape of these failures is worth recording: each one looked
like a different bug (IDF version, transitive dep, code error),
but all three were really the component manager's version-solve
phase running ahead of the system that defines its inputs. Pinning
the component aggressively and aligning Kconfig symbols with the
component's actual schema is the cure; chasing each surface error
in isolation would have rolled forward into v1.4 of the same bug.

#### Path-filter mechanics

The workflow declares `paths: ['firmware/**', '.github/workflows/firmware.yml']`
on both `push` and `pull_request` triggers. GitHub Actions evaluates
this against the changed files in each event:

- A server-side change → no firmware files touched → workflow not
  triggered → no Docker image pull, no compile.
- A firmware code change → workflow triggered → full IDF build.
- A change to `firmware.yml` itself → workflow triggered (so
  workflow edits get tested before merging).

The matched-path-but-skipped-job semantics from earlier YAMLs are
not in play here — this is a single-job workflow with first-class
path filtering on the trigger.

#### Why the ESP-IDF action over manual install

The Espressif-maintained action runs the build inside an
`espressif/idf:release-v5.4` Docker image that already has:

- The xtensa toolchain at the right version
- The Python environment with `idf-component-manager`
- Every native dependency the toolchain needs

Manually installing ESP-IDF in a CI step is ~1.5 GB of clones +
a Python venv setup + a long warm-up. The Docker image pulls in
~30 s on a warm GitHub runner cache, vs. ~3 min for a fresh
install. That cost difference matters when the workflow runs
on every PR.

#### Phase 4.4 — what shipped

The five drivers delivered across 4.4.1–4.4.4, all building clean
under `-Wall -Wextra -Werror` and passing the project's
`.clang-format` profile:

- **Relay** (4.4.2) — single-output relay with latched `set()`
  and pulsed `pulse(duration)` modes, mapping directly to the
  proto's `LATCH_OPEN` / `LATCH_CLOSE` / `PULSE_RELAY` /
  `OPEN_GATE` / `CLOSE_GATE` commands.
- **LimitSwitch** (4.4.2) — debounced GPIO input with 5 ms poll
  + 20 ms debounce, NO/NC polarity abstraction, callback fires
  from the timer task.
- **SafetyBeam** (4.4.4) — same poll+debounce primitive tuned
  for safety: 1 ms poll + 5 ms debounce (~6 ms detection
  latency), failsafe-aligned default polarity.
- **StatusLed** (4.4.4) — three-color LED pattern renderer with
  six modes matching the proto's `LedPattern` enum (off,
  auth/deny flashes, fault/OTA blinks, boot-OK solid).
- **Ethernet** (4.4.3) — W5500 SPI driver wrapper using the
  `espressif/ethernet_init` managed component; one-shot
  `start()` brings up DHCP and surfaces link/IP callbacks.

The `gate-firmware` binary is **332 KB**, occupying 22 % of the
1.5 MB OTA partition — leaving comfortable room for the gRPC
client (~400 KB), state machine (~50 KB), and OTA update
machinery (~80 KB) that Phase 4.5 will bring online.

#### What Phase 4.5 will need

The state machine in 4.5 will instantiate one `Relay` (or two for
dual-leaf swing gates), two `LimitSwitch` (open + closed), one
`SafetyBeam`, one `StatusLed`, and call `Ethernet::start()` to
bring up the network. The gate's pin map will live in a
configuration header so the state machine can be reused across
physical board layouts.

The gRPC client side (consuming the `Control` bidi stream from
4.3.5) is the big new piece in 4.5 — protobuf-c + nanopb-grpc
or `gRPC for ESP-IDF` (which uses HTTP/2 over LWIP TCP). The
firmware will open the `Control` stream on `Ethernet::on_got_ip`,
send `Telemetry` heartbeats, receive `GateCommand`s, and ack
each one back via the same stream.

OTA delivery (the `DeliverOta` and `ReportOtaProgress` RPCs that
returned `UNIMPLEMENTED` in Phase 4.3.5) becomes real in 4.5,
backed by ESP-IDF's `esp_https_ota` over the same gRPC channel.

---

## Repository layout

```
gate-automation/
├── docs/                  # ADRs, diagrams, hardware build guides, env audit
│   ├── decisions/         # 11 ADRs (ADR-000 through ADR-010)
│   ├── diagrams/          # 7 Mermaid flowcharts
│   └── hardware/          # 9 component guides + master build book + KiCad PDF
├── hardware/
│   └── bom/               # Bill of materials with KES/USD pricing
├── shared/
│   ├── proto/             # ✅ Phase 4.1 — gRPC wire contract
│   └── include/           # Shared C++ headers
├── server/                # ✅ Phase 4.2/4.3 — ALPR + LiDAR inference + fusion + RPC
│   ├── inference/         # ✅ Phase 4.2 — TensorRT engines + ALPR pipeline
│   ├── auth/              # ✅ Phase 4.3.1 — allowlist + blocklist store
│   ├── fusion/            # ✅ Phase 4.3.2 — verdict-ladder decision engine
│   ├── dash/              # ✅ Phase 4.3.3 — dashboard event broadcaster
│   ├── rpc/               # ✅ Phase 4.3.4/5 — gRPC services + lifecycle wrapper
│   └── src/main.cpp       # ✅ Phase 4.3.5 — gate-server daemon entry point
├── firmware/              # 🔵 Phase 4.4 — ESP-IDF field controller firmware (ESP32-S3)
│   ├── main/              # ✅ Phase 4.4.1 — app_main entry component
│   ├── components/
│   │   └── gate_drivers/  # 🔵 Phase 4.4.2-4.4.4 — relay/limit/eth/safety/LED drivers
│   ├── partitions.csv     # Two-OTA 4 MB layout (nvs, otadata, ota_0/1, spiffs)
│   └── sdkconfig.defaults # Compile-time pinning (target=esp32s3, freertos, OTA)
├── simulation/            # ⏳ Phase 4.6 — virtual gate harness
├── dashboard/             # ⏳ Phase 4.7 — Drogon backend + SvelteKit frontend
├── deployment/            # ⏳ Phase 4.8 — systemd units + install scripts
├── tests/                 # Catch2 unit + contract tests
│   ├── proto/             # ✅ Phase 4.1 — proto contract tests
│   ├── inference/         # ✅ Phase 4.2.6 — host-side algorithm tests
│   ├── auth/              # ✅ Phase 4.3.1 — allowlist store tests
│   ├── fusion/            # ✅ Phase 4.3.2 — fusion engine tests
│   ├── dash/              # ✅ Phase 4.3.3 — broadcaster fan-out tests
│   ├── rpc/               # ✅ Phase 4.3.4/5 — service handler + lifecycle tests
│   └── integration/       # ✅ Phase 4.3.6 — end-to-end gRPC roundtrip tests
├── scripts/bootstrap/     # Reproducible WSL2 dev environment scripts
└── .github/workflows/     # CI: build, test, lint, codeql, commitlint, proto
```

---

## Hardware

Single-gate prototype BOM:

- **Gate-side electronics:** KES 58,300 (~USD 448)
- **GPU server (RTX 4060 + i5):** KES 85,200 (~USD 655)
- **System total without motor:** **KES 143,500 (~USD 1,103)**
- **With sliding gate motor (CENTURION D5):** KES 208,500 (~USD 1,603)

Full BOM with sources and prices: [`hardware/bom/prototype-bom.md`](hardware/bom/prototype-bom.md).

Build book (9 step-by-step component guides + master assembly): [`docs/hardware/BUILD_BOOK.md`](docs/hardware/BUILD_BOOK.md).

Production PCB: 4-layer 100×80 mm field controller designed in KiCad 9.0
— [48-page design guide PDF](docs/hardware/10-pcb-design-kicad9.pdf).

---

## Tech stack

| Layer | Tools |
|---|---|
| Compute | NVIDIA GPU (CUDA 13.1, cuDNN 9.19, TensorRT 10.15) |
| Inference | YOLOv9 (plate detection), PaddleOCR (character recognition) |
| Vision | OpenCV 4.14 (CUDA-enabled, source build) |
| Server (C++) | C++20, CMake 3.31, Ninja, mold, ccache, vcpkg manifest mode |
| RPC | gRPC + Protobuf (single canonical schema in `shared/proto/`) |
| Dashboard backend | Drogon (C++ web framework) |
| Dashboard frontend | SvelteKit |
| Field controller | ESP32-S3-WROOM-1-N16R8 + W5500 wired Ethernet |
| Firmware | ESP-IDF, modern C++ |
| OTA | Self-hosted, ed25519-signed, atomic swap with rollback |
| Tests | Catch2 v3, ASan/UBSan, clang-tidy, cppcheck, CodeQL |
| CI | GitHub Actions (matrix: GCC 13/14 × Clang 17/18 × Debug/Release) |

---

## Running it locally

### Bootstrap the dev environment (one-time, WSL2 Debian)

```bash
cd scripts/bootstrap
./01-toolchain.sh        # GCC, Clang, CMake, Ninja, mold, ccache
./02-cuda-stack.sh       # CUDA 13.1 + cuDNN 9.19 + TensorRT 10.15
./03-opencv-cuda.sh      # OpenCV 4.14 from source with CUDA
./04-vcpkg.sh            # vcpkg + manifest install
./05-esp-idf.sh          # ESP-IDF for firmware development
```

Verify with the GPU smoke test (see [`docs/env-audit.md`](docs/env-audit.md)).

### Validate the proto schema

```bash
python3 tests/proto/validate_descriptor.py
```

### Configure & build (Phase 4.1)

```bash
cmake --preset release
cmake --build --preset release --target gate_proto
ctest --preset release
```

---

## Documentation

- [Environment audit](docs/env-audit.md) — every tool, every version, every challenge solved during bootstrap
- [Architecture decisions (ADRs)](docs/decisions/) — 11 records covering license, RPC, web framework, MCU, LiDAR, camera, gate actuator, OTA, model licensing
- [System diagrams](docs/diagrams/) — 7 Mermaid flowcharts (system, ALPR, LiDAR, fusion, gate state machine, OTA, sim mode)
- [Hardware build book](docs/hardware/BUILD_BOOK.md) — bench prototype to working gate in ~3.5 h
- [PCB design guide (PDF)](docs/hardware/10-pcb-design-kicad9.pdf) — KiCad 9.0 schematic-to-Gerbers
- [Bill of materials](hardware/bom/prototype-bom.md) — every part with KES + USD pricing and Nairobi/AliExpress sources

---

## License

[GPL-3.0](LICENSE) — chosen for compatibility with YOLOv9's GPL-3.0 license
(see [ADR-000](docs/decisions/ADR-000-license.md) and
[ADR-010](docs/decisions/ADR-010-model-licensing.md)).

## Contributing

This is a personal portfolio project. External contributions are not yet
accepted, but feedback via GitHub issues is welcome.
