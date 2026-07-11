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
| **4.5** | **Firmware app (state machine, gRPC client, OTA)** | ✅ **Complete** |
| &nbsp;&nbsp;&nbsp;&nbsp;4.5.1 | Gate state machine skeleton | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.5.2 | Wire state machine to drivers on ESP32-S3 | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.5.3 | gRPC client foundation (Control bidi stream) | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.5.4 | Telemetry heartbeats + GateCommand handling | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.5.5 | OTA delivery via `esp_https_ota` | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.5.6 | Firmware integration tests + Phase 4.5 closure | ✅ Complete |
| 4.6 | Simulation harness | ✅ Complete |
| **4.7** | **Dashboard backend + frontend** | ✅ **Complete** |
| &nbsp;&nbsp;&nbsp;&nbsp;4.7.1 | Backend foundation: Drogon app + gRPC bridge + health | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.7.2 | REST API: commands, allowlist CRUD | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.7.3 | WebSocket live event stream + status snapshot | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.7.4 | SvelteKit frontend scaffold + live monitoring view | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.7.5 | Frontend allowlist + controls + Phase 4.7 closure | ✅ Complete |
| 4.8 | Deployment scripts (systemd, install, OTA signing) | ✅ Complete |
| 4.9 | End-to-end integration tests | ✅ Complete |
| **4.10** | **Security hardening (TLS/mTLS, JWT admin auth)** | 🔄 **In progress** |
| &nbsp;&nbsp;&nbsp;&nbsp;4.10.1 | Host-side TLS/mTLS: server, dashboard, sim, site PKI tooling | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.10.2 | JWT admin authentication | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.10.3 | Firmware TLS (esp-tls) + HTTPS OTA | ⏳ Pending — next |
| &nbsp;&nbsp;&nbsp;&nbsp;4.10.4 | Phase 4.10 closure | ⏳ Pending |

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

## Phase 4.4 complete — firmware CI + driver suite delivered

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

## Phase 4.5.1 — Gate state machine skeleton

> **Completed 2026-06-22.** New component at
> [`firmware/components/gate_state_machine/`](firmware/components/gate_state_machine/).
> Host tests at [`tests/state_machine/`](tests/state_machine/).

### What I built

A pure C++20 state machine that owns the gate's transition logic
without touching any ESP-IDF API. Eight states (`Initializing`,
`Closed`, `Opening`, `Open`, `Closing`, `StoppedOpen`, `StoppedClose`,
`Faulted`), eleven edge-triggered events, eleven driver actions, and
five fault/stop reasons that surface in telemetry. The same translation
unit compiles for the ESP32-S3 (as an IDF component) and for the host
Catch2 suite (as a vanilla static library) — 11 tests covering the
happy path, operator stop / resume, the safety-beam latch, motor
timeout → fault, fault reset, and the `Initializing` drop-everything
boot state. All green on the first push.

This is the first half of Phase 4.5's split: 4.5.1 keeps the logic
hardware-agnostic so tests can pin it down; 4.5.2 wires it to the
real relays, limit switches, safety beam, and LED driver on the
ESP32-S3.

| Artifact | Purpose |
|---|---|
| `firmware/components/gate_state_machine/include/gate_state_machine/state_machine.hpp` | Public API. Defines `State`, `Event`, `Action`, `Reason`, `Config`, `Outputs`, and the `StateMachine` class. No `<esp_*.h>` or FreeRTOS includes — host-clean. |
| `firmware/components/gate_state_machine/src/state_machine.cpp` | Flat-switch transition table for all 8 × 11 legal `(state, event)` combinations, plus `to_string()` overloads for telemetry/log serialisation. |
| `firmware/components/gate_state_machine/CMakeLists.txt` | ESP-IDF component registration. No `REQUIRES` — it has no IDF dependencies, which is the point. |
| `firmware/host/CMakeLists.txt` | New host-side mirror. Declares the `gate_state_machine` static library by pointing `add_library` at the same source file under `firmware/components/`, so on-target and host builds compile the exact same translation unit. |
| `tests/state_machine/state_machine_test.cpp` | 11 Catch2 cases covering boot init, the happy path, operator stop, resume, beam latch, timeout fault, fault reset, and the `Initializing` drop-events guard. Uses `Catch::Matchers::Equals` on the action list so failures show a readable diff. |
| `firmware/main/main.cpp` | Constructs a `static StateMachine` in `app_main()` and logs its boot state. Not yet driven by hardware — 4.5.2 wires the event sources. |

### Technical detail

#### Why a pure-C++ module with no ESP-IDF deps

The state machine is the brain of the gate. Embedding `ESP_LOG*`,
`xQueueSend`, or `esp_timer_get_time` inside it would make every
transition both untestable on the host and dependent on hardware
state. The skeleton inverts that: events are pure data, actions are
pure data, the transition function reads `(state, reason, event,
beam_clear_)` and returns the next state plus a small action list.
That gives three immediate wins:

1. **Host unit tests run in the same CI matrix as everything else.**
   No QEMU emulator, no `esp-idf-test` setup, no flaky timing. The
   `gate_state_machine` static library links into the existing Catch2
   binary chain alongside `gate_auth`, `gate_fusion`, `gate_dash`,
   etc., and runs in microseconds.
2. **4.5.2 can swap event sources without touching state logic.**
   The driver layer will produce events from limit-switch ISRs, gRPC
   `GateCommand`s, and the auto-close timer; the state machine
   doesn't care where an `Event::CommandOpen` came from.
3. **Phase 4.7's simulation harness gets the same brain for free.**
   The simulator can drive the state machine programmatically with no
   firmware target needed — the same transitions, the same fault
   handling, the same telemetry.

#### Why timers don't appear in the `Action` enum

A first cut included `StartMotorTimer` / `CancelMotorTimer` /
`StartAutoCloseTimer` / `CancelAutoCloseTimer` actions. I removed
them. Reason: the state machine knows *what* state the gate is in
but does not know *how long* anything should take. The driver layer
owns hardware timers (`esp_timer`-backed), so it can derive timer
start/stop from `(new_state, Config)` itself — `Opening` arms the
runtime watchdog, `Open` arms the auto-close timer if
`Config::auto_close_ms != 0`, the inverse transitions cancel.

Dropping those four actions also let `kMaxActionsPerStep` shrink from
4 to 3 (worst case is now `DriveMotor* + SetLed* + EmitTelemetry`),
which keeps `Outputs` a 6-byte struct + a 3-byte array on the stack —
small enough that the FreeRTOS event-pump task in 4.5.2 won't need
to heap-allocate anything per tick.

#### How the safety-beam latch works

The skeleton tracks `beam_clear_` as a private bool that updates on
every `SafetyBeamTripped` / `SafetyBeamCleared` event, regardless of
state. The reason it's stateful instead of "look up the IR sensor
each time": at the state-machine layer there is no sensor — only the
last edge that the driver layer reported. If the beam breaks mid-`Open`,
then a `CommandClose` arriving later (e.g. an auto-close timer that
the driver layer doesn't know about) must be rejected synchronously,
not after another I/O round-trip. The latch enforces "no Close while
the last beam edge was Tripped" without needing the driver layer to
re-poll.

A beam break during `Closing` also stops the gate immediately
(`StoppedClose`, reason `SafetyBeamObstacle`). The skeleton stops
only — it does not auto-reverse. Auto-reverse is a deployment-policy
choice (some installs want it, some don't) and belongs in 4.5.2
where operator config is available.

#### Why `FaultCleared` returns to `Initializing` instead of the last known state

When a motor timeout faults the gate, the actual physical position is
unknown — the motor was driving but never hit a limit switch. Could be
stuck halfway, could be that the limit switch failed, could be a
wiring fault. Re-entering `Initializing` forces the driver layer to
re-read both limit switches and call one of `init_closed()`,
`init_open()`, or `init_unknown()` again before any motion. If both
limits are released the machine returns to `Faulted` via
`init_unknown()` with `Reason::LimitSwitchConflict`, which the
operator sees in telemetry as "you need to physically check the gate".

#### The shape of the host-side mirror

`firmware/host/CMakeLists.txt` is a new directory whose only job is to
declare `add_library(gate_state_machine STATIC …)` pointing at
`../components/gate_state_machine/src/state_machine.cpp`. The
top-level `CMakeLists.txt` pulls it in with
`add_subdirectory(firmware/host)`, and `tests/CMakeLists.txt` then
adds the test subdirectory gated on `if(TARGET gate_state_machine)`
— same defensive pattern the existing `gate_proto` / `gate_auth` /
`gate_fusion` / `gate_dash` / `gate_rpc` test wiring uses.

That layout means the ESP-IDF build never sees `firmware/host/`
(its `project.cmake` doesn't walk that path), and the host build
never imports the IDF component descriptor. Both sides reach the
same translation unit through different CMake graphs, which is what
keeps the contract honest: a bug introduced in
`state_machine.cpp` shows up in CI in the next push, regardless of
which side broke first.

#### What 4.5.2 will add on top

1. A FreeRTOS task that owns the `StateMachine` instance, pulls
   events off a queue, and dispatches the returned `Action` list to
   `Relay::open()` / `Relay::close()` / `StatusLed::set_pattern()`.
2. Limit-switch / safety-beam ISR handlers that push `Event` values
   onto that queue (`xQueueSendFromISR`).
3. Two `esp_timer` handles: one for the motor-runtime watchdog, one
   for auto-close. Both derive their schedule from `Config`.
4. A boot-time limit-switch read that decides which of
   `init_closed()` / `init_open()` / `init_unknown()` to call before
   the event loop starts pumping.
5. A `vTaskDelay`-driven heartbeat that logs the current state so the
   first hardware bring-up has a serial-console signal that the brain
   is alive.

---

## Phase 4.5.2 — Wiring the state machine to the drivers

Phase 4.5.1 built a brain with no body: a pure state machine that
turns Events into Actions but touches no hardware and reads no clock.
This sub-milestone is the body — a new `gate_control` component whose
single class, `GateController`, owns the drivers, the event queue, the
timers, and the FreeRTOS task that pumps them. `app_main` now
constructs the controller, calls `start()`, and gets out of the way:
from that point the gate is fully reactive to its limit switches,
safety beam, and command entry points.

### Artifacts

| Artifact | What it does |
|---|---|
| `firmware/components/gate_control/include/gate_control/gate_controller.hpp` | `GateController` — public API: `start()`, thread-safe `command_open()` / `command_close()` / `command_stop()` / `clear_fault()` entry points, and lock-free `state()` / `reason()` snapshots. Carries the reference DevKitC-1 pin map as `Pins` defaults. |
| `firmware/components/gate_control/src/gate_controller.cpp` | The event pump task, the Action dispatcher (relays / LED / telemetry-log), the motor watchdog + auto-close timers, boot-time position resolution, and the two driver-layer policies (reverse-on-beam, auto-close retry). |
| `firmware/components/gate_control/CMakeLists.txt` | IDF component registration. ESP-IDF-only by design — the hardware-agnostic logic stays in `gate_state_machine`, which is what the host Catch2 suite exercises. |
| `firmware/components/gate_drivers/{include/gate_drivers/status_led.hpp, src/status_led.cpp}` (updated) | Three new firmware-local LED patterns: `kGateMoving` (2.5 Hz yellow), `kGateOpen` (solid green, timer parked), `kGateStopped` (1 Hz red/yellow alternation). Values 0–5 still mirror the proto `LedPattern` one-to-one; the new values start at 6, outside the wire range. |
| `firmware/main/main.cpp` (updated) | Replaces the 4.5.1 "construct and log" placeholder with a `static GateController` + `ESP_ERROR_CHECK(start())`. Config: sliding gate, 30 s motor watchdog, auto-close off until remote config lands (4.5.4), reverse-on-beam on. |
| `shared/proto/gate_service.proto` + `tests/proto/proto_contract_test.cpp` (updated) | Drive-by durability fix (own commit): `OtaChunk.final` renamed to `is_final`. protobuf's C++ keyword-escaping for `final` changed across protoc versions (`final_()` → `final()`), so the generated API depended on the toolchain. Same field number — wire-compatible. |

### The threading model: five producers, one consumer, zero locks

Every input converges on a single FreeRTOS queue drained by one task
(`gate_ctrl`, 4 KB stack, priority 5):

1. **Limit switches** — `LimitSwitch` edge callbacks post
   `LimitOpenHit/Released`, `LimitClosedHit/Released`.
2. **Safety beam** — `SafetyBeam` edge callbacks post
   `SafetyBeamTripped/Cleared`.
3. **Motor watchdog** — an `esp_timer` one-shot posts `MotorTimeout`.
4. **Auto-close** — a second one-shot posts a plain `CommandClose`.
5. **Command entry points** — `command_*()` / `clear_fault()`, callable
   from any task (today `app_main`; in 4.5.4, the gRPC stream task).

The 4.5.1 README predicted ISR handlers and `xQueueSendFromISR`; the
actual drivers made that unnecessary. `LimitSwitch` and `SafetyBeam`
debounce by polling from the **esp_timer task** — their callbacks are
ordinary task context, and `esp_timer` expiry callbacks run there too,
so every producer uses plain `xQueueSend` and the "ISR-safe" API never
appears. Because only the pump task ever touches the `StateMachine`,
the machine needs no mutex; the `state()`/`reason()` snapshots other
tasks read are a pair of relaxed atomics the pump refreshes after
every step.

Queue depth is 16 for five producers of edge-triggered events — a full
queue means something is deeply wrong, so `post()` logs the drop rather
than blocking (a blocked esp_timer task would stall every debouncer in
the system).

### Timer ownership, made concrete

4.5.1 deliberately kept wall-clock time out of the state machine; this
is the other half of that contract. `manage_timers()` runs after every
transition:

- **Motor watchdog** — armed with the full `motor_timeout_ms` budget on
  every entry into `Opening`/`Closing` (a resume from `Stopped*` gets
  the full budget again; travel from mid-position is strictly shorter,
  so the bound still holds), cancelled the moment motion ends. Expiry
  posts `MotorTimeout`, which the state machine turns into
  `Faulted`/`MotorTimeout` — the "gate jammed on a stone" path.
- **Auto-close** — armed on entering `Open` when
  `Config::sm.auto_close_ms > 0`, cancelled on leaving it. It fires a
  plain `CommandClose` through the same queue as an operator command,
  so it hits the same beam-latch rejection.

### Boot: the gate never moves from an unverified position

`start()` sleeps 50 ms (covers the slowest debounce commit: 20 ms at a
5 ms poll), pre-latches the beam (`SafetyBeamTripped` is recorded even
in `Initializing`, so a boot-time obstruction rejects the first close),
then reads both limit switches once:

- closed asserted, open not → `init_closed()`
- open asserted, closed not → `init_open()`
- **anything else** → `init_unknown()` → `Faulted`. Both-asserted is a
  wiring fault; neither-asserted means the gate sat mid-travel through
  a reboot. Either way an operator must `clear_fault()`, and the pump
  task answers `FaultCleared → Initializing` by re-running the same
  resolution — so there is exactly one code path that can ever declare
  a position, and it always reads the physical switches first.

Edge callbacks are registered *after* resolution so the queue starts
from a clean slate, and the pump task is spawned last.

### Policies live in the driver layer, not the state machine

Two operator-policy decisions that 4.5.1 explicitly deferred land here,
in `apply_policies()`:

- **Reverse-on-beam** (default **on**, matching UL 325
  entrapment-protection expectations for residential operators): when
  the beam trips during `Closing`, the state machine stops the motor
  (`StoppedClose`/`SafetyBeamObstacle`); the controller then posts
  `CommandOpen`, and the ordinary `StoppedClose → Opening` transition
  drives the gate away from the obstruction. Opening with the beam
  still blocked is legal by design — the machine only gates *closing*
  on the latch.
- **Auto-close retry**: an auto-close `CommandClose` that arrives while
  the beam is blocked is rejected (gate stays `Open`, no transition) —
  but the one-shot has already burned. Without a re-arm the gate would
  stay open forever, so the controller re-arms the countdown for
  another full period and the retry loop ends when a close finally
  goes through.

Both are pure driver-layer behaviours: the state machine's transition
table is untouched, and the 21-test host suite still passes unchanged.

### Action dispatch details worth keeping

- **Break-before-make relay interlock**: `DriveMotorOpen` drops the
  close contactor before picking the open one (and vice versa), so the
  two coils are never energised together regardless of the Action
  order the state machine emitted.
- **LED mapping**: `SetLedClosed → kOff` (an idle secured gate shows no
  light), `SetLedOpening/Closing → kGateMoving` (2.5 Hz yellow — fast
  enough to read as "in motion" next to the 1 Hz `kOtaPulse`),
  `SetLedOpen → kGateOpen` (solid green; the renderer parks its 100 ms
  timer since a steady colour needs no animation), `SetLedStopped →
  kGateStopped` (red/yellow alternation: attention, not fault),
  `SetLedFault → kFaultSlow` (pure red, 1 Hz).
- **Telemetry placeholder**: `EmitTelemetryStateChanged` logs
  `state -> X (reason=Y)` on the serial console until the gRPC Control
  stream (4.5.3/4.5.4) gives it a wire to ride.
- **Heartbeat**: the pump's `xQueueReceive` timeout doubles as the
  heartbeat — every 5 s of quiet it logs state, reason, beam status,
  and both limit readings, so first hardware bring-up has a pulse to
  watch before any wiring is proven.

### Why the proto fix rode along

Configuring the host build fresh for this milestone surfaced that the
local vcpkg toolchain had drifted to protobuf 6.33, whose generated
accessor for a field named `final` changed from `final_()` to
`final()` — breaking `tests/proto` locally while CI (which builds the
C++ test tree only when the vcpkg toolchain is present) stayed green.
Renaming the field to `is_final` removes the keyword collision for
every protoc version at once and is wire-compatible (field number
unchanged). It shipped as its own commit so the contract change is
visible in history rather than buried in a firmware diff.

### Verification

- `idf.py build` (ESP-IDF v6.1, esp32s3): clean; binary at 0x556f0
  bytes, 78 % of the smallest OTA app partition still free.
- Host suite: 21/21 Catch2 tests green (11 state-machine + proto
  contract), descriptor validator green.

#### What 4.5.3 will add on top

The gRPC client foundation: a Control bidi-stream connection to the
server, brought up when `Ethernet`'s `on_got_ip` fires, feeding
`GateCommand`s into the same `command_*()` entry points the RPC layer
was designed around.

---

## Phase 4.5.3 — gRPC client foundation

The plan said "gRPC client" and the honest engineering answer was
that ADR-002's consequence line — *"both server and firmware link
against `grpc++`"* — was written before anyone tried to fit `grpc++`
(plus abseil, re2, c-ares, and the full protobuf runtime) into an
MCU with 4 MB of flash. It does not fit, and there is no ESP-IDF
port. What *does* fit is gRPC the **protocol**, which is just three
well-specified layers. This sub-milestone implements all three and
opens a real `FieldControllerService/Control` bidi stream against the
unmodified server. **ADR-011** documents the deviation and supersedes
that one line of ADR-002.

### The three-layer stack

| Layer | gRPC needs | Firmware answer |
|---|---|---|
| Messages | protobuf encode/decode | **nanopb 0.4.9** (`livekit/nanopb` managed component): plain-C runtime, statically-allocated structs sized by `shared/proto/gate_service.options` |
| Framing | 5-byte prefix per message (compressed flag + big-endian length) | first-party codec in `gate_rpc` — pure C++20, host-mirrored, 9 Catch2 tests |
| Transport | HTTP/2 | **nghttp2** (`espressif/nghttp`) over a plain lwIP socket — the server listens with `InsecureServerCredentials`, i.e. cleartext h2c, so the client preface goes straight onto the TCP connection with no TLS and no Upgrade dance |

`espressif/sh2lib` was evaluated for the transport layer and rejected:
it hardwires esp-tls with ALPN `h2`, which cannot produce an h2c
connection. The replacement is ~150 lines of socket + poll() pump in
`control_client.cpp`, and dropping esp-tls from the dependency chain
is why the whole stack costs ~114 KB of flash (0x556f0 → 0x71490,
with 70 % of the OTA slot still free).

### Artifacts

| Artifact | What it does |
|---|---|
| `docs/decisions/ADR-011-firmware-grpc-transport.md` | The transport decision: options table, why sidecar bridges and grpc-web lost, the TLS/mTLS upgrade path. |
| `shared/proto/gate_service.options` | nanopb static sizing for every firmware-touched message (Telemetry, GateCommand, CommandAck, FaultEvent, Ota*). Fields without sizes fall back to callback type at zero RAM cost — the firmware never touches those messages. |
| `scripts/gen-nanopb.sh` | Deterministic regeneration: venv-pinned `nanopb==0.4.9` (matching the runtime component; `PB_PROTO_HEADER_VERSION` fails the build on drift), system protoc, well-known types from `/usr/include`. Output is **committed** so CI and clean checkouts build with no Python toolchain. |
| `firmware/components/gate_rpc/src/pb/` | The committed generator output: `gate_service.pb.{h,c}` + timestamp/duration/empty. Excluded from the clang-format CI sweep — machine-formatted, not ours to style. |
| `firmware/components/gate_rpc/{include/gate_rpc/grpc_framing.hpp, src/grpc_framing.cpp}` | `make_frame_header()` + incremental `FrameAssembler`: caller-supplied buffer, re-entrant `push()` across arbitrary DATA-chunk boundaries, poisoned-until-reset on the two protocol violations (compressed flag, oversize declaration). |
| `tests/rpc_framing/` | 9 host tests: byte-at-a-time delivery, chunk boundary inside the 5-byte header, three messages in one chunk, empty message, poison/reset, oversize-rejected-before-buffering. Host target is `gate_rpc_framing` (`gate_rpc` was already the server-side RPC library). |
| `firmware/components/gate_rpc/{include/gate_rpc/control_client.hpp, src/control_client.cpp}` | `ControlClient`: the "gate_rpc" task (8 KB stack, priority 4 — below gate_ctrl, RPC yields to safety), connect → stream → pump loop, exponential-backoff reconnect (1 s → 30 s, reset after any session that got response HEADERS), thread-safe `send_envelope()` through a FreeRTOS queue, `to_wire_state()` mapping `sm::State` → proto `GateState`. |
| `firmware/main/Kconfig.projbuild` | `GATE_ID`, `GATE_SERVER_HOST`, `GATE_SERVER_PORT` — server address is deployment config, not code. |
| `firmware/main/main.cpp` (updated) | Constructs the client before ethernet start (no got-ip edge can slip past the wiring), `on_got_ip → notify_network_up()`, link-down → `notify_network_down()`. |
| `shared/proto/gate_service.proto` + 5 C++ users (own commit) | `VehicleDetection.class` renamed to `vehicle_class` — `class` is a C++ keyword, so protoc escapes it (`class_()`) and nanopb, which doesn't escape at all, emitted a header that wasn't valid C++. Same field number, wire-compatible; same disease and cure as `OtaChunk.final` in 4.5.2. |

### How the pump works

One long-lived HTTP/2 session per connection, one gRPC stream per
session, everything on the gate_rpc task (nghttp2 sessions are not
thread-safe; the queue is the only cross-thread boundary):

1. Block on an event-group bit until DHCP delivers an address.
2. `getaddrinfo` → `connect()` → `TCP_NODELAY` → non-blocking.
3. `nghttp2_submit_settings` + `nghttp2_submit_request` with the gRPC
   headers (`:method POST`, `:path /gate.v1.FieldControllerService/
   Control`, `content-type: application/grpc`, `te: trailers`).
4. Queue the **hello Telemetry** envelope (gate_id, mapped state,
   uptime, heap stats, fw version, seq) so it rides out right behind
   the HEADERS frame.
5. Pump: `poll()` at 250 ms; inbound bytes → `nghttp2_session_mem_recv`
   → DATA chunks → `FrameAssembler` → nanopb decode → today, a log
   line per received `GateCommand`. Outbound: the data-provider
   callback drains the current frame and returns `DEFERRED` when dry;
   the pump resumes the stream when the queue refills.
6. Any exit — GOAWAY, stream close, socket error, framing violation —
   tears the session down and re-enters the backoff loop.

Two deliberate holds for 4.5.4: `sent_ts` stays unset until SNTP
exists (a zero timestamp is worse than an absent one), and Stopped*
states report `GATE_STATE_UNKNOWN` because the wire enum has no
mid-travel value — extending the contract is a server-side modelling
decision, not something the firmware should improvise.

### Verification

- `idf.py build` clean (esp32s3, nghttp 1.69 + nanopb 0.4.9 fetched as
  managed components); binary 0x71490, 70 % OTA headroom.
- Host suite: 30/30 (state machine 11, framing 9, proto contract),
  descriptor validator green after the field rename.

#### What 4.5.4 will add on top

The wire goes live end-to-end: SNTP + real `sent_ts`, the 1 Hz
telemetry cadence with limit/beam snapshots from `GateController`,
`GateCommand` dispatch into the `command_*()` entry points, and
`CommandAck` (received + completed) flowing back.

---

## Phase 4.5.4 — Telemetry cadence + GateCommand execution

4.5.3 opened the pipe; this sub-milestone makes it carry the actual
product: telemetry at the proto's ~1 Hz cadence with real hardware
snapshots and wall-clock timestamps, and full GateCommand execution
with the double-ack contract (`completed=false` on receipt,
`completed=true` with success/error/state_after when execution
finishes). The interesting design question was *what "finishes" means
for a command that physically takes fifteen seconds* — and the answer
became another pure, host-tested module.

### CommandTracker: completion is four rules

An async motion command (`OPEN_GATE`, `CLOSE_GATE`) is done when one
of four things happens, and every one of them is pure logic over
`(state, reason, transitioned, time)`:

1. **Success** — the gate transitioned into the commanded terminal
   state (`Open` / `Closed`).
2. **Fault** — the gate transitioned into `Faulted` while the command
   ran; the ack's error is the fault reason (`MotorTimeout`, …).
3. **Interlock refusal** — the step carried a `Reason` without a
   transition: the safety-beam latch rejecting a close. The server
   hears `rejected: safety beam obstacle` instead of a mystery timeout.
4. **Deadline** — nothing terminal within `command_deadline_ms`
   (default 40 s = motor watchdog budget + slack, so rule 2 normally
   wins with a better error). Wrap-safe `uint32` arithmetic.

One command in flight at a time — a gate cannot execute two motion
commands at once, so arming over a live command supersedes it and the
old command_id gets a failure ack (never a silently-abandoned ack; the
`Verdict` carries a *copy* of the id, because arm() both returns the
old verdict and overwrites the id buffer — the aliasing-pointer bug
was caught while writing the supersession test). `CommandTracker` has
no FreeRTOS/nanopb/nghttp2 anywhere: it joins the state machine and
framing codec in the host suite (7 new tests, 37 total).

### How events reach the tracker

`GateController` grew a `TransitionListener` invoked from its pump
task after any step that transitioned **or** was rejected with a
`Reason` — that "or" is exactly what rule 3 needs, since a beam-latch
rejection changes nothing but must still resolve an ack. main wires
the listener to `ControlClient::notify_gate_event()`, which updates
the shared `last_wire_state_` (so `CommandAck.state_after` and
telemetry never disagree about the gate) and consults the tracker
under its mutex. Deadlines ride the pump's 250 ms poll tick.

### Dispatch policy (wired in main, not in the client)

| CommandKind | Handling |
|---|---|
| `OPEN_GATE` / `CLOSE_GATE` | `command_open()` / `command_close()`; async, terminal `Open` / `Closed` |
| `LED_PATTERN` | `show_led_pattern()` (wire values 0–5 cast straight onto `StatusLed::Pattern`); synchronous ack |
| `REBOOT` | ack first, `esp_restart()` 1.5 s later via one-shot — a blocking delay in the dispatcher would stall the HTTP/2 pump and the ack would never flush |
| `BEGIN_OTA` | honest failure: "OTA delivery lands in Phase 4.5.5" |
| `PULSE_RELAY` / `LATCH_*` / `RELEASE_LATCH` | honest failure: the residential profile drives two motion contactors directly — there is no third-party opener behind a trigger relay |

### Telemetry, now with a clock

- ~1 Hz cadence on the pump loop (wrap-safe compare, 250 ms jitter
  bound), each message carrying mapped state, both limit switches, the
  beam (new lock-free snapshot accessors on `GateController`), uptime,
  heap stats, fw version, and a monotonic `seq`.
- SNTP via `esp_netif_sntp` (server in Kconfig, default pool.ntp.org),
  started right after ethernet. `sent_ts` / `received_ts` /
  `completed_ts` are filled **only once the clock reads past ~2023** —
  until sync, timestamps are absent rather than epoch garbage.

### Artifacts

| Artifact | What it does |
|---|---|
| `firmware/components/gate_rpc/{include/gate_rpc/command_tracker.hpp, src/command_tracker.cpp}` | The four completion rules + supersession; pure C++20. |
| `tests/rpc_framing/command_tracker_test.cpp` | 7 tests incl. wrap-around deadlines and supersession id integrity. |
| `firmware/components/gate_rpc/control_client.*` (updated) | Telemetry cadence, wall-clock fills, `send_ack_received/completed`, `handle_command` (immediate ack → dispatch → arm tracker), `notify_gate_event`. |
| `firmware/components/gate_control/gate_controller.*` (updated) | `TransitionListener` (fires on transitions and reasoned rejections), `limit_open_active()` / `limit_closed_active()` / `beam_blocked()` snapshots, `show_led_pattern()`. |
| `firmware/main/main.cpp` + `Kconfig.projbuild` (updated) | Dispatcher table above, listener wiring (before `start()`, asserted), SNTP init, reboot one-shot, `GATE_SNTP_SERVER`. |

Build note: GCC's `-Werror=stringop-truncation` (fires at -O2, so the
IDF build caught what the -O0 host build didn't) rejected the
`strncpy(dst, src, cap-1)` idiom — replaced with `strnlen` + `memcpy`,
which states the intent (bounded copy, explicit NUL) instead of
pattern-matching to the classic truncation bug.

### Verification

- `idf.py build` clean; binary 0x75680 (+16 KB over 4.5.3), 69 % OTA
  headroom.
- Host suite: 37/37 (state machine 11, framing 9, tracker 7, proto
  contract), format sweep clean.

#### What 4.5.5 will add on top

OTA delivery: `BEGIN_OTA` stops returning a polite refusal —
`DeliverOta` streams `OtaChunk`s into the passive partition via
`esp_https_ota`/`esp_ota_ops`, progress flows back over
`ReportOtaProgress`, and `kOtaPulse` finally earns its LED slot.

---

## Phase 4.5.5 — OTA delivery

The 4.5.4 closing note guessed the transport wrong, and ADR-009 —
written back in Phase 2 — is what actually governs: updates come from
a **self-hosted static file server** (JSON manifest + image binary,
ed25519-signed, A/B partitions with automatic rollback), not from the
proto's `OtaChunk` gRPC stream. The gRPC OTA methods stay stubbed
server-side; chunk streaming over the Control connection would have
put a 4 KiB-per-message hop through the inference server's process for
something a static HTTP GET does better. `BEGIN_OTA` is now a real
command: it triggers the ADR-009 pipeline and acks through the
`external_completion` path added to the dispatcher contract.

### The pipeline (new `gate_ota` component)

```
BEGIN_OTA ─▶ GET manifest.json ─▶ validate (pure rules) ─▶ esp_https_ota
             (esp_http_client,      version differs ·        stream into
              cJSON)                fits passive slot ·      passive slot
                                    uptime gate · sha/sig    (kOtaPulse LED)
                                    fields present
        ─▶ flash readback SHA-256 ─▶ ed25519 verify ─▶ finish() ─▶ ack ─▶ reboot
           (libsodium, hashes what    (libsodium, over     boot slot   1.5 s later
            was actually written)      the digest)          flips
```

Design points worth remembering:

- **Manifest rules are pure and host-tested** (`ota_manifest.*`, 7 new
  tests, suite now 44): hex decoding with trailing-garbage rejection,
  version-idempotence guard (re-flashing the running version is
  refused), image ≤ passive slot, and the manifest's own
  `min_uptime_sec` gate — a device crash-looping through short uptimes
  refuses to take an update, per the proto's production-safety note.
- **Verify what hit the flash, not what crossed the wire**: the SHA-256
  runs over a readback of the written partition, so a flash-level
  corruption fails the update before the boot slot flips.
- **ed25519 via libsodium** — deliberately not mbedtls: IDF v6 ships
  mbedtls 4 (PSA-first, legacy md API in flux) and mbedtls has no
  EdDSA anyway; libsodium gives both `crypto_sign_verify_detached` and
  `crypto_hash_sha256` in one stable dependency. Until the Phase 4.8
  deployment keypair exists (`GATE_OTA_PUBKEY` empty), verification
  falls back to the checksum alone — with a loud warning, never
  silently.
- **Rollback handshake**: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
  makes a freshly-flashed image boot as `PENDING_VERIFY`; `app_main`
  marks it valid only after drivers + controller + RPC are all up, so
  a crash loop anywhere in bring-up reverts to the previous slot with
  no operator involvement.
- **Ack-then-reboot**: success acks ride the stream first; the restart
  fires 1.5 s later off the same one-shot the REBOOT command uses.
  Failures ack with the precise pipeline error (`manifest: image
  larger than the OTA partition`, `verify: ed25519 signature
  rejected`, …) and flash the deny pattern.
- **`DispatchResult.external_completion`**: a third dispatch mode next
  to sync and tracker-tracked — the subsystem owns its own completion
  and deadline, and calls `ControlClient::complete_command()` when
  done. OTA is its first user.

### Artifacts

| Artifact | What it does |
|---|---|
| `firmware/components/gate_ota/{include/gate_ota/ota_manifest.hpp, src/ota_manifest.cpp}` | Manifest struct (schema documented in the header), `hex_decode`, `validate` — pure C++20, host-mirrored as `gate_ota_manifest`. |
| `firmware/components/gate_ota/{include/gate_ota/ota_updater.hpp, src/ota_updater.cpp}` | `OtaUpdater` — the six-step pipeline on a one-shot "gate_ota" task (priority 3, below RPC and gate control). One update at a time via CAS. |
| `tests/ota_manifest/` | 7 host tests over the accept/reject rules. |
| `firmware/main/{main.cpp, Kconfig.projbuild}` (updated) | Real `BEGIN_OTA` dispatch, ack/reboot/LED wiring, rollback mark-valid at end of bring-up; `GATE_OTA_MANIFEST_URL` + `GATE_OTA_PUBKEY`. |
| `firmware/sdkconfig.defaults` (updated) | `BOOTLOADER_APP_ROLLBACK_ENABLE`, `ESP_HTTPS_OTA_ALLOW_HTTP` (plain http on the field LAN until 4.8 provisions TLS — integrity comes from the digest + signature, not the transport). |
| `firmware/components/gate_ota/idf_component.yml` | `espressif/libsodium` + `espressif/cjson` — cJSON left the IDF core in v6, which the first build caught. |

### Verification

- `idf.py build` clean; binary 0xb79d0 (+270 KB — libsodium,
  esp_https_ota, http client, cJSON), 52 % OTA headroom.
- Host suite 44/44; format sweep clean.

#### What 4.5.6 will add on top

Phase 4.5 closure: firmware integration tests (driving the pure
modules through realistic end-to-end scenarios — boot → connect →
command → ack sequences), plus the phase retrospective in this log.

---

## Phase 4.5.6 — Firmware integration scenarios + Phase 4.5 closure

The unit suites pin each pure module alone; this sub-milestone pins
the **composition** — the places where a contract drift between
modules would hide. `tests/firmware_integration/` introduces
`SimulatedGate`: a host-side harness that owns a `StateMachine` and a
`CommandTracker` and replays, in pure C++, exactly the wiring the
ESP32-S3 runs — `execute()`'s motor bookkeeping, `manage_timers()`'s
watchdog/auto-close arming, `apply_policies()`'s reverse-on-beam and
auto-close re-arm, the transition listener into the tracker, and the
pump's periodic tick — on top of a toy physics model (an energised
motor hits its limit switch after `travel_ticks`).

### The six scenarios

1. **Remote open lifecycle** — command → tracker armed → travel →
   limit hit → success ack, motors off.
2. **Auto-close dwell** — the gate closes itself; the policy retries
   are not commands, so exactly one ack ever leaves.
3. **Beam trip mid-close** — the pending close fails *immediately*
   with `SafetyBeamObstacle`, reverse-on-beam drives back to Open, the
   blocked auto-close is latched out and re-arms, and once the beam
   clears the gate finally closes. One walk-through, five module
   contracts exercised.
4. **Motor stall** — watchdog → `Faulted`, failure ack carries
   `MotorTimeout`, and `FaultCleared` lands in `Initializing` (the
   4.5.2 re-verify-position contract).
5. **Operator stop mid-open** — immediate abort ack with
   `OperatorStop`; a fresh command resumes and succeeds.
6. **Unreachable command** — a close issued while already Closed is
   dropped by the state machine, so only the deadline can end it.

### The bug the scenarios found before writing a single one

Sketching scenario 3 exposed a hole in the 4.5.4 tracker: a commanded
close aborted by the beam *mid-travel* is a **transition** (Closing →
StoppedClose), not a latched rejection — and no rule matched it. The
server would have stared at a silent command for the full 40 s
deadline and then received a useless "deadline exceeded". The tracker
now has five rules instead of four: **rule 3 — aborted mid-travel** —
any transition into `StoppedOpen`/`StoppedClose` fails the pending
command right away with the carried reason (`OperatorStop` /
`SafetyBeamObstacle`). Two new unit tests pin it alongside the six
scenarios. This is the integration-test thesis in one anecdote: both
modules were individually correct, and the composition still had a
16-second-wait bug.

### Artifacts

| Artifact | What it does |
|---|---|
| `tests/firmware_integration/gate_scenarios_test.cpp` | `SimulatedGate` harness + the six scenarios (suite: 44 → 51). |
| `firmware/components/gate_rpc/{command_tracker.hpp, command_tracker.cpp}` (updated) | Rule 3 (mid-travel abort); rules renumbered, five total. |
| `tests/rpc_framing/command_tracker_test.cpp` (updated) | The old "stop leaves the command pending" expectation replaced by two abort tests. |

### Verification

- Host suite 51/51; `idf.py build` clean (0xb79e0, 52 % OTA headroom);
  format sweep clean.

---

### Phase 4.5 closure — what the firmware app phase delivered

Six sub-milestones took the board from "boot banner + idle loop" to a
connected, commandable, updatable field controller:

| | Sub-milestone | The load-bearing idea |
|---|---|---|
| 4.5.1 | State machine skeleton | Pure Events→Actions logic; no IDF, no clock — testable anywhere |
| 4.5.2 | Driver wiring | One queue, one consumer, zero locks; timers and policies live in the driver layer |
| 4.5.3 | gRPC client | gRPC-the-protocol ≠ grpc++-the-library: nanopb + framing codec + nghttp2 h2c (ADR-011) |
| 4.5.4 | Telemetry + commands | Double-ack contract; command completion as pure rules (CommandTracker) |
| 4.5.5 | OTA | ADR-009 pipeline: manifest → validate → stream → readback-hash → ed25519 → A/B flip + rollback |
| 4.5.6 | Integration scenarios | Test the composition; it found the mid-travel abort gap |

Numbers: four first-party components (`gate_state_machine`,
`gate_control`, `gate_rpc`, `gate_ota`), 51 host tests over the pure
mirrors, binary 0x556f0 → 0xb79e0 (752 KB, 52 % of an OTA slot free),
three proto contract fixes forced by toolchain realities
(`is_final`, `vehicle_class`, the nanopb `.options` sizing), and two
new ADR-grade decisions (ADR-011 transport; ADR-009 executed as
written).

Deliberately left open, with owners: TLS/mTLS on both the Control
stream and OTA transport plus the ed25519 deployment keypair (Phase
4.8); server-side `ReportOtaProgress`/`DeliverOta` remain stubbed
(the PCB path doesn't use them — a future Linux field unit might);
a wire enum value for mid-travel "stopped" states (server-side
modelling, revisit with the dashboard); remote gate config —
auto-close dwell, gate type — over the Control stream.

Next: **Phase 4.6 — simulation harness**, where the `SimulatedGate`
idea grows into a standalone virtual gate that speaks the real gRPC
contract against the real server.

---

## Phase 4.6 — Simulation harness: gate-sim

The 4.5.6 test harness promised on its way out that it would grow up;
one milestone later it did. `simulation/` ships **gate-sim**, a
standalone virtual gate field controller: the same pure
`gate_state_machine` and `CommandTracker` the ESP32-S3 runs (linked
from the `firmware/host` mirrors — the sim executes the *identical
translation units*), composed with the same GateController wiring,
driven by wall-clock physics, and speaking the wire contract over
**real grpc++** against a real `gate-server`. The server, dashboard
(Phase 4.7), and fusion pipeline can now be developed and demoed
against a live-feeling gate — or a whole fleet of them, one process
per `--gate-id` — with no hardware on the desk.

### What it is

```
gate-sim --server 192.168.1.10:50051 --gate-id gate-sim-01 \
         --travel-ms 12000 --auto-close-ms 8000 --interactive
```

- **`VirtualGate`** (`gate_sim_core`): position-based physics — 0 ms
  is the closed limit, `travel_ms` the open limit, motion integrates
  real elapsed time. A resume after a mid-travel stop therefore takes
  exactly the remaining distance, which makes watchdog and auto-close
  interactions behave like a machine rather than a state chart.
  Reboot and fault-clear re-resolve state from the simulated physical
  position, mirroring the 4.5.2 boot contract (a mid-travel reboot
  lands in `Faulted` — realistically).
- **`SimClient`**: one Control-stream session per connection using the
  same generated stubs as the server; 1 Hz telemetry with real
  timestamps, double acks, tracker-driven completion for motion
  commands, honest failures for BEGIN_OTA ("simulator has no flash to
  update") and the latch/pulse kinds. A `LockedWriter` serialises the
  session loop and the command handler onto gRPC's
  one-write-in-flight rule.
- **`main.cpp`**: CLI11 flags, SIGINT/SIGTERM-clean shutdown, a
  reconnect loop whose physics keep running while disconnected (an
  offline gate still moves), and `--interactive` stdin commands —
  `open close stop trip clear fault-clear reboot status quit` — so a
  human can walk someone through the beam while the server watches
  the telemetry change.

### Firmware ↔ simulator symmetry

| Layer | ESP32-S3 firmware | gate-sim |
|---|---|---|
| Gate logic | `gate_state_machine` | same translation unit |
| Command completion | `CommandTracker` | same translation unit |
| Controller wiring | `GateController` (FreeRTOS) | `VirtualGate` (std::mutex + caller clock) |
| Messages | nanopb | protobuf C++ |
| Transport | nghttp2 h2c (ADR-011) | grpc++ |
| Physics | a real motor | `position += dt` |

The two columns share their brain and differ only in body — which is
the whole point: a behaviour observed against gate-sim is a behaviour
the firmware will exhibit, unless the bug is in a driver.

### Proven over the wire, in CI

`tests/simulation/` spins an **in-process gRPC server** with a mock
`FieldControllerService` (the mock needs only `gate_proto` — no
server/ build, no GPU) and runs the full exchange through a real
HTTP/2 connection: telemetry arrives stamped and typed → the mock
issues `OPEN_GATE` → received ack (`completed=false`) → 300 ms of
simulated travel → completed ack (`success`, `state_after=OPEN`) →
the virtual gate is physically at its open limit. Suite: 51 → 52.

### Build wiring

`BUILD_SIM=ON` by default, but the target set only materialises when
vcpkg resolves `gate_proto` + CLI11 — the same "no vcpkg → no targets"
invariant that keeps the lint-only CI configure inert. The
`firmware/host` mirrors moved up so simulation and tests share them
(guarded against double-add), and `simulation/` joined the
clang-format and cppcheck sweeps in the Lint workflow.

### Verification

- Host suite 52/52 (roundtrip test: 0.36 s against a live in-process
  gRPC server).
- Manual: `gate-sim --server 127.0.0.1:59999` boots Closed, retries on
  a 3 s cadence, exits cleanly on SIGINT; `--help` documents every
  knob.

#### What Phase 4.7 will add on top

The dashboard: Drogon backend subscribing to `DashboardService`, a
SvelteKit frontend — developed against a gate-sim fleet instead of
waiting for field hardware.

---

## Phase 4.7.1 — Dashboard backend foundation

Phase 4.7 opens with the sub-milestone split above and the Drogon
skeleton everything else hangs off. The backend is deliberately a
**thin bridge process**: REST + WebSocket southbound to the browser,
one gRPC channel northbound to the gate-server — no business logic,
no database; the server owns truth, the dashboard presents it.

### Artifacts

| Artifact | What it does |
|---|---|
| `dashboard/backend/CMakeLists.txt` | Three targets: `gate_dash_json` (presentation mapping, Drogon-free), `gate_dash_api` (GrpcBridge, adds Drogon), `gate-dashboard` (executable). Root CMake gates on vcpkg-resolved `gate_proto` + Drogon — the lint-configure-stays-inert invariant again. `BUILD_DASHBOARD` now defaults ON. |
| `include/dash_api/json_mapping.hpp` + `src/json_mapping.cpp` | The **presentation schema**: proto → camelCase JSON, enum short names (`GATE_STATE_OPEN` → `OPEN`, out-of-range → raw integer, never a crash), dual timestamps (ISO-8601 `ts` for humans, epoch `tsMs` for sorting), `DashboardEvent` oneof → `type` + typed body. protobuf's reflection-based JSON was rejected on purpose — the wire schema and the UI schema should be allowed to evolve independently, and this file is the explicit contract between them. |
| `include/dash_api/grpc_bridge.hpp` + `src/grpc_bridge.cpp` | One channel, both stubs (DashboardService + AdminService) — gRPC multiplexes over a single HTTP/2 connection, so one channel is the right number. Drogon creates controllers reflectively (no constructor args), so a process-wide `set_bridge()/bridge()` pair wired in main is the hand-off. |
| `src/controllers/health_controller.cpp` | `GET /api/health` → `{status, upstream, upstreamAddr, siteId}`. Answers even when the gate-server is down — a dashboard that can say "server unreachable" beats a dead one. |
| `src/main.cpp` | CLI11 flags (`--listen/--port/--server/--site-id/--threads/--cors-dev`); the CORS flag exists only for the Vite dev server origin — production serves the built SPA same-origin from this process. |
| `tests/dashboard_api/` | 6 tests pinning the presentation schema (suite 52 → 58): field names, enum stripping, timestamp encoding, oneof dispatch, diagnostic fields on acks/faults. The frontend codes against exactly these shapes. |

### Two lessons the build taught

- **Drogon controllers must not live in static libraries.** They
  register through global constructors; the linker drops unreferenced
  archive members, and the symptom is a silent 404 on a route that
  compiles fine. Controllers now compile straight into the
  executable, with the reason recorded in the CMakeLists.
- **`option()` defaults don't beat a warm cache.** Flipping
  `BUILD_DASHBOARD` to ON changed nothing until `-DBUILD_DASHBOARD=ON`
  updated the existing cache — worth remembering for every future
  option-default change.

### Verification

- 58/58 host tests; format + cppcheck sweeps clean (dashboard sources
  were already inside both CI sweeps).
- Live boot: `gate-dashboard --port 18099 --server 127.0.0.1:59998` →
  `GET /api/health` returns
  `{"siteId":"site-01","status":"ok","upstream":false,…}` — healthy
  backend honestly reporting a dead upstream; clean SIGINT shutdown.

#### What 4.7.2 will add on top

The REST surface: `POST /api/gates/{id}/command` → `IssueCommand`,
allowlist CRUD proxied to `AdminService`, and a `GET /api/status`
snapshot fed by the event cache that 4.7.3's Subscribe consumer will
maintain.

---

## Phase 4.7.2 — Dashboard REST API

The REST surface the frontend will call, three controllers deep. One
re-slice against the 4.7.1 plan: `GET /api/status` moved to 4.7.3 —
it reads the event cache that only exists once the Subscribe consumer
does, and shipping a placeholder that returns nothing real would have
been décor. The timeline row is updated accordingly.

### The surface

| Route | Upstream | Notes |
|---|---|---|
| `POST /api/gates/{gateId}/command` | `DashboardService.IssueCommand` | Body `{"kind": "OPEN_GATE" \| "CLOSE_GATE" \| "LATCH_OPEN" \| … , "ledPattern"?}`. The server stamps `command_id`/`issued_ts` and answers with the initial `CommandAck`; the response is that ack under the presentation mapping, so the frontend correlates the eventual completion (arriving on the 4.7.3 stream) by `commandId`. |
| `GET /api/allowlist?pageSize=&pageToken=` | `AdminService.ListAllowlist` | Paged; bridge stamps `site_id`. |
| `POST /api/allowlist` | `AdminService.UpsertAllowlist` | One entry object, or `{"entries":[…]}` for a batch; bridge stamps `addedBy`/`addedTs` defaults. |
| `DELETE /api/allowlist/{plate}` | `AdminService.DeleteAllowlist` | Single plate. |

### Plumbing worth recording

- **gRPC → HTTP status mapping** (`controllers/grpc_http.hpp`):
  `INVALID_ARGUMENT`→400, `NOT_FOUND`→404, `DEADLINE_EXCEEDED`→504,
  `UNAVAILABLE`→503, `UNAUTHENTICATED/PERMISSION_DENIED`→403,
  everything unrefinable→502. Every error body is `{"error": …}` with
  the upstream detail preserved — the live check against a dead
  server returns `503 {"error":"failed to connect … Connection
  refused"}`, which is exactly what an operator debugging a broken
  deployment wants to read in the network tab.
- **Every upstream call carries a 2 s deadline** — a Drogon IO thread
  blocks at most that long against a dead server. Synchronous stubs
  are a deliberate simplicity trade at single-site scale (2 IO
  threads, LAN); the note lives in the bridge header for whoever
  scales it.
- **Inbound validation is pure and tested**:
  `allowlist_entry_from_json` (plate required, vehicle classes parsed
  through protobuf's `VehicleClass_Parse` with `VEHICLE_CLASS_`
  re-prefixing, time-window minutes bounded to 0..1439, day masks
  clamped to 7 bits) and `command_kind_from_string` (UNSPECIFIED is
  rejected, not defaulted). Three new test cases pin the reject
  paths with their exact error strings; suite 58 → 61.

### Verification

- 61/61 host tests; format + cppcheck sweeps clean.
- Live against a dead upstream: unknown kind → `400 {"error":"unknown
  command kind: MAKE_COFFEE"}`, plateless entry → `400`, command/list
  → `503` with the gRPC connect error preserved.

#### What 4.7.3 will add on top

The live half: a Subscribe consumer with reconnect + ring cache,
`/ws/events` WebSocket fan-out of the presentation-mapped events, and
the deferred `GET /api/status` snapshot on top of that cache.

---

## Phase 4.7.3 — WebSocket live event stream + status snapshot

The live half of the backend, split along the project's standing
fault line: everything with policy in it is a pure, host-tested class;
the I/O around it is thin glue.

### EventCache (pure) + EventStream (the pump)

**`EventCache`** is the backend's memory: a ring of the most recent
presentation-mapped frames (each event serialised to compact JSON
exactly once, however many browsers are attached), the latest
telemetry per gate, the WebSocket sink registry, and the highest
`event_id` seen. Four unit tests pin fan-out (exactly once per sink,
detach honoured), ring ordering/eviction, per-gate snapshot
supersession, and the resume cursor being the *highest* id seen — a
replayed duplicate must not rewind it.

**`EventStream`** owns one `DashboardService::Subscribe` stream on a
background thread for the life of the process. When the stream dies it
reconnects with 1 s → 30 s backoff and passes
`cache().last_event_id()` back as `since_event_id`, so the server's
replay ring (Phase 4.3.3) fills the gap — a dashboard that survives a
server restart without losing or duplicating events. That exact
scenario runs in CI: a mock in-process gRPC server writes events 1–3,
kills the stream, and the test asserts the second subscription arrives
with `since_event_id=3` and the cache ends at 5 with all frames in
the ring.

### The endpoints on top

- **`/ws/events`** — on connect: replay the ring (a freshly opened
  dashboard paints instantly), then register a sink holding a *weak*
  connection pointer (a closed socket can never be written through a
  dangling handle). Frames flow from the EventStream thread through
  trantor's thread-safe queue-posting `send()`. `ping` → `pong` for
  naive keepalives. Verified with a raw handshake:
  `HTTP/1.1 101 Switching Protocols` + correct `Sec-WebSocket-Accept`.
- **`GET /api/status`** (deferred here from 4.7.2, now real) — the
  cache's per-gate snapshot plus `streamConnected` and `upstream`
  flags, so the UI can badge staleness instead of presenting dead
  data as live.

### A CI-portability catch

Newer cppcheck releases count the informational
`normalCheckLevelMaxBranches` notice ("analysis depth limited")
against `--error-exitcode` — locally rc=1 with zero actual findings,
while CI's older cppcheck stayed green. The Lint workflow now
suppresses that specific id with a comment; the suppression hides an
analysis-depth notice, not a defect class.

### Verification

- 66/66 host tests (event cache ×4, stream reconnect/resume over real
  gRPC ×1, plus the existing suites).
- Live boot: `/api/status` →
  `{"gates":{},"lastEventId":0,"streamConnected":false,"upstream":false}`
  against a dead server; `/ws/events` upgrade handshake → 101.

#### What 4.7.4 will add on top

The first pixels: SvelteKit SPA scaffold (ADR-004), gate status tiles
off `/api/status`, and the live event feed off `/ws/events` — built
against a gate-sim fleet.

---

## Phase 4.7.4 — SvelteKit frontend: scaffold + live monitoring

First pixels. `dashboard/frontend/` is now a real SvelteKit app in
SPA mode (ADR-004): `adapter-static` with an `index.html` fallback so
the built site can be served same-origin by the Drogon process — or
any static server — with client-side routing from there. Svelte 5,
TypeScript strict, and deliberately **zero runtime dependencies**
beyond the framework: no component library, no state manager, no
fetch wrapper. At this surface area they would each cost more than
they pay.

### Structure

| Piece | What it does |
|---|---|
| `src/lib/types.ts` | The presentation schema transcribed for the compiler — the same shapes `tests/dashboard_api/` pins on the C++ side, camelCase fields, enum short names, `{ts, tsMs}` stamps. |
| `src/lib/live.ts` | The connection layer: one WebSocket to `/ws/events` with 1 s → 15 s reconnect; on every (re)connect the stores re-seed from `GET /api/status`, so the UI converges after a backend restart — the same resume-not-gap philosophy the backend applies to its own upstream. Downstream it's plain Svelte stores (`gates`, `feed`, three connection flags); components stay dumb. |
| `src/lib/GateTile.svelte` | Per-gate card: state-coloured edge (CLOSED green, OPEN blue, moving amber, FAULT red, LOCKDOWN purple), limit/beam facts with a loud `BEAM BLOCKED`, fw/uptime/data-age footer, and a staleness fade past 10 s without telemetry. |
| `src/lib/EventRow.svelte` | One feed line per event with a per-type summary (decision → verdict+plate, ack → ✓/✗ with `stateAfter` or error, fault → severity+code); faults and failed acks render red. |
| `src/routes/+page.svelte` | Header with three honesty badges (backend / event stream / server — the UI distinguishes *its* connection from the backend's upstream), the tile grid, and the capped live feed. |
| `vite.config.ts` | Dev proxy: `/api` + `/ws` → the Drogon backend, so development is same-origin and `--cors-dev` stays optional. |
| `.github/workflows/dashboard.yml` | Frontend CI: `npm ci` → `svelte-check` → production build, path-filtered to `dashboard/frontend/**` so JS churn and C++ churn don't burn each other's CI minutes. |

### Verification

- `svelte-check`: 0 errors, 0 warnings across 145 files (TypeScript
  strict).
- Production build → static `build/` output; served via
  `vite preview` and answers `200` with the app shell.
- The live-data path (tiles moving while a gate-sim fleet feeds the
  server) is the Phase 4.7.5 closure demo, which is also where
  command buttons land on the tiles.

#### What 4.7.5 will add on top

Controls and administration: open/close buttons on the tiles wired to
`POST /api/gates/{id}/command`, the allowlist management page over the
4.7.2 CRUD, and the Phase 4.7 closure.

---

## Phase 4.7.5 — Frontend controls + allowlist + Phase 4.7 closure

The dashboard becomes an actor, not just an observer.

### What landed

- **`src/lib/api.ts`** — typed REST helpers that resolve to
  `{ok, error?}` instead of throwing: the callers are UI event
  handlers, and a fetch failure is a state to render, not an
  exception to forget to catch. Backend `{"error"}` bodies surface
  verbatim.
- **Command buttons on the tiles** — Open/Close →
  `POST /api/gates/{id}/command`. The inline result shows only
  "accepted": the *initial* ack means accepted, the completion ack
  arrives on the live feed, and the tile's colour follows telemetry —
  the UI never pretends a 15-second physical process finished because
  an HTTP call returned.
- **`/allowlist` page** — table over `GET /api/allowlist`, add-entry
  form (plate normalised to uppercase client-side, vehicle-class
  checkboxes with "none selected = any class", owner/unit/notes),
  per-row remove. Validation errors from the backend's pure parser
  land next to the form with their exact reason.
- **Shared `+layout.svelte`** — header, nav, and the three honesty
  badges moved out of the page; the WebSocket lives in the layout so
  navigating Monitor ↔ Allowlist doesn't tear the connection down.

### Verification

- `svelte-check`: 0 errors / 0 warnings (149 files); production build
  clean.
- Dev-path E2E: Vite dev server proxying to a **live** gate-dashboard
  backend — `/` 200, `/allowlist` 200, `/api/status` through the
  proxy returns the backend's honest empty snapshot.

---

### Phase 4.7 closure — what the dashboard phase delivered

Five sub-milestones, two languages, one contract:

| | Sub-milestone | The load-bearing idea |
|---|---|---|
| 4.7.1 | Backend foundation | Thin bridge process; presentation schema as an explicit, tested contract |
| 4.7.2 | REST API | gRPC↔HTTP semantics mapped once, honestly; pure inbound validation |
| 4.7.3 | Live stream | EventCache/EventStream split; resume-not-gap on reconnect, proven against a mock server in CI |
| 4.7.4 | Frontend scaffold | SPA with zero extra runtime deps; the schema transcribed for the compiler |
| 4.7.5 | Controls + allowlist | Accepted ≠ completed in the UI; admin CRUD end to end |

Numbers: three C++ targets + one SvelteKit app, 66 host tests (15
dashboard-specific), a fifth CI workflow (path-filtered frontend), and
one wire-contract philosophy — resume, don't gap — applied at every
hop of the chain: firmware → server → backend → browser.

Deliberately open, with owners: authentication/authorization on both
the REST surface and AdminService (JWT per ADR-003 — Phase 4.8
deployment hardening, alongside TLS everywhere); serving the built
SPA from Drogon's document root (a deployment concern, 4.8); the full
three-process live demo (gate-server + gate-sim fleet + dashboard)
needs the GPU host and lands with Phase 4.9's end-to-end integration
pass.

Next: **Phase 4.8 — deployment** (systemd units, install scripts, the
OTA signing keypair, TLS).

---

## Interlude — gate-mock-server: the GPU-free demo stack

An unplanned tool, built on request and kept because it earns its
place: `gate-mock-server` (in `simulation/`) is a stand-in for
gate-server implementing exactly the RPC surface the rest of the
system consumes — `FieldControllerService/Control` bridging gate-sim
fleets, `DashboardService/Subscribe` + `IssueCommand` for the
dashboard, an in-memory `AdminService` allowlist, and a synthetic
ALPR decision every 15 s so the feed breathes. No inference, no
fusion, no persistence, no auth — demo-grade by intent and labelled
as such.

```bash
gate-mock-server --listen 127.0.0.1:50051 &
gate-sim --server 127.0.0.1:50051 --gate-id gate-sim-01 --travel-ms 8000 --auto-close-ms 12000 &
gate-sim --server 127.0.0.1:50051 --gate-id gate-sim-02 --travel-ms 15000 &
gate-dashboard --port 8080 --server 127.0.0.1:50051 --www dashboard/frontend/build
# → http://localhost:8080
```

Verified live end to end: both sims connect and stream telemetry, the
dashboard paints tiles and the event feed, and a browser Open button
drives `CLOSED → OPENING → OPEN` through five processes and three
protocols, with the auto-close bringing it home. This is also the
scaffold Phase 4.9's integration pass will reuse — swap the mock for
the real gate-server on the GPU host and the other four processes
don't change.

---

## Phase 4.8 — Deployment: units, installer, OTA signing

Everything a Phase-2 placeholder promised, made real — plus one scope
decision made out loud. The old stubs (`deployment/install.sh` echoing
TODO, a nonsensical `gate-firmware.service` for a chip that has no
systemd, an `update_server.hpp` that lost to nginx the day ADR-009 was
written) are deleted, not renovated.

### The OTA signing toolchain — the part that has to be byte-exact

`scripts/gen-ota-keys.sh` (openssl ed25519 keypair; prints the raw
32-byte public key as the exact hex string `GATE_OTA_PUBKEY` expects,
extracted from the DER SubjectPublicKeyInfo tail; refuses to overwrite
an existing key) and `scripts/sign-ota-manifest.sh` (image → signed
`manifest.json` per the gate_ota schema). The convention that must
match `OtaUpdater::run_inner` exactly: **the ed25519 message is the
raw 32-byte SHA-256 digest of the image** — the same digest the
firmware recomputes from its flash readback. The signer self-verifies
before writing, and verification went one step further here: the
manifest's hex fields plus the Kconfig-format pubkey were
reconstructed into DER and verified *from the published artifacts
alone* — the exact bytes a device will trust. Coreutils-only
(`od`/`tr`, `openssl dgst -binary`) after the first run discovered the
build host has no `xxd`.

### Units, installer, topology

- `deployment/systemd/gate-server.service` + `gate-dashboard.service`:
  hardened (NoNewPrivileges, ProtectSystem=strict, service account,
  StateDirectory), env-file-driven so units never need editing, and a
  crash-loop brake that doubles as the server-side ADR-009 rollback
  posture. `systemd-analyze verify` caught a real bug in the first
  draft: `StartLimit*` moved to `[Unit]` scope in systemd 230 and is
  *silently ignored* in `[Service]` — the exact kind of quiet
  misconfiguration the verify step exists for.
- `deployment/install-server.sh`: service account, `/opt/gate/bin`
  with previous binaries kept as `*.prev` for manual rollback, SPA to
  `/opt/gate/www`, `/etc/gate/*.env` installed first-run-only so
  upgrades never clobber operator edits, units enabled. Idempotent.
- `deployment/nginx/gate-ota.conf`: the ADR-009 static file server —
  `/srv/gate/firmware` on :8081, manifests `no-cache` (a gate polls
  that file to learn an update exists), everything else 404.
- `deployment/README.md`: the one-page topology + install + release
  walkthrough.

### The SPA is now served same-origin

`gate-dashboard --www <dir>` sets Drogon's document root with an SPA
fallback: non-`/api`/`/ws` 404s return `index.html` as 200 so client
routes survive deep links and reloads, while API 404s stay honest
404s. Live-verified end to end: root 200 with the app shell, deep-link
`/allowlist` falls back to index, `/api/health` still routed, hashed
`_app/immutable/*` assets served. The 4.7 "production is same-origin"
promise is now mechanical fact.

### Scope decision: security hardening is its own milestone

TLS/mTLS (gRPC + OTA transport) and JWT on the admin API were parked
with "owner: 4.8" — and doing them *inside* 4.8 would have meant
touching server credentials, the dashboard channel, gate-sim, and the
firmware's transport (nghttp2 → esp-tls) in one sprawling change. The
timeline now carries an explicit **Phase 4.10 — security hardening**
row so the system upgrades transport everywhere at once instead of
living half-TLS. Today's documented posture: isolated field LAN,
integrity carried by signatures rather than transport.

### Verification

- Signing roundtrip: generate → sign → openssl self-verify →
  independent re-verify from the manifest JSON + Kconfig pubkey hex.
- `systemd-analyze verify` clean on both units (after the StartLimit
  fix it caught).
- SPA-from-Drogon: five live checks (root, deep link, API routing,
  honest API 404, hashed assets).
- Suite 66/66; format + cppcheck sweeps clean.

#### What 4.9 will add on top

The end-to-end integration pass on the GPU host: gate-server +
gate-sim fleet + dashboard, all three processes on real sockets, plus
the full-stack scenarios (plate → decision → gate motion → dashboard
paint) that no single-seam test can cover.

---

## Phase 4.9 — End-to-end integration tests

The 4.8 closing note assumed this phase needed the GPU host. It
mostly didn't — and discovering why reshaped the build.

### The unlock: the server splits cleanly on the GPU boundary

Every server library except `inference/` was *already* documented as
CPU-only (auth, fusion, dash, rpc — and `gate-server` itself links
none of TensorRT). The only thing forcing `BUILD_SERVER` onto a GPU
machine was a CMake `FATAL_ERROR` guarding the whole subtree.
`BUILD_SERVER` now defaults ON and works with `ENABLE_GPU=OFF` —
`inference/` alone stays gated — which produced two immediate payoffs:

1. **64 dormant tests woke up.** The auth, fusion, dash, RPC-handler,
   and gRPC-roundtrip suites had *never compiled in this environment*
   (CI configures without vcpkg; local builds had `BUILD_SERVER=OFF`).
   Suite: 66 → 130, all green on first run — including the code the
   4.5.3 `vehicle_class` rename edited blind.
2. **The real daemon runs locally.** No mock needed for the
   integration pass; the genuine `FieldControllerServiceImpl`,
   fusion engine, and SQLite-backed AdminService sit at the middle of
   the stack.

### The E2E pass itself

`tests/e2e/e2e_local_stack.py` (stdlib-only, ctest-registered, 10 s):
launches **gate-server + two gate-sims + gate-dashboard** on loopback
and drives the stack exactly like a browser would, asserting the
seams no single-process test covers:

1. **liveness** — `/api/health` reports the upstream channel up;
2. **convergence** — both sims' telemetry crosses sim → server →
   backend cache and lands in `/api/status` as CLOSED;
3. **allowlist CRUD** — REST upsert/list/delete round-trips through
   the real AdminService into real SQLite;
4. **command lifecycle** — `POST OPEN_GATE` (acked with a real
   server-minted UUIDv4) physically drives one gate
   CLOSED → OPENING → OPEN → auto-close → CLOSED while the
   *unaddressed* gate provably never moves;
5. **teardown** — all four processes exit cleanly on SIGINT.

Suite total: **131**.

### What the pass caught before any host did

The 4.8 systemd unit invoked `gate-server --db …`; the actual flag is
`--db-path`. Deployment would have failed at first `systemctl start`.
The unit is fixed with the catch recorded inline — integration work
earning its keep on day one.

### The genuinely GPU-bound remainder

The inference path — camera frame → TensorRT ALPR →
`SubmitDetection` → fusion verdict — still needs the GPU host and a
camera, and stays explicitly open alongside firmware-on-bench
hardware validation. Everything downstream of a detection (fusion,
decisions, commands, telemetry, dashboard) is now exercised
end-to-end on every developer machine.

Next: **Phase 4.10 — security hardening** (TLS/mTLS everywhere, JWT
admin auth) — the final planned Phase 4 milestone.

---

## Phase 4.10.1 — Host-side TLS/mTLS

Everything on the host side of the gRPC fabric now speaks TLS — and,
in production posture, *mutual* TLS: the server proves itself to every
client and every client proves itself back with a certificate from
the site CA.

### The server's three-step ladder

`gate::rpc::TlsConfig` rides in `ServerConfig`, and its fields form a
deliberate ladder rather than a pile of independent knobs:

| Given | You get |
|---|---|
| `--tls-cert` + `--tls-key` | TLS: the server proves itself, clients unauthenticated |
| … + `--tls-ca` | client certs verified *if presented* |
| … + `--require-client-cert` | mTLS: no valid client cert, no connection |

Two postures are non-negotiable. **Unreadable PEM = refuse to start**
— a server asked for TLS never silently falls back to plaintext; it
logs which file failed and exits. And plaintext-by-omission stays
legal for dev/tests but announces itself with a startup warning, so a
production box misconfigured back to plaintext is loud in the journal.

### Both host clients grew the mirror image

The dashboard bridge and gate-sim take `--tls-ca` (trust the site CA —
this alone turns TLS on) plus `--tls-cert`/`--tls-key` (their client
identity for mTLS). The sim's wiring deliberately mirrors the posture
the ESP32 firmware will adopt in 4.10.3 — the sim keeps its job as the
firmware's stand-in, now for transport security too.

### One command mints the site PKI

`scripts/gen-tls-certs.sh` generates an ed25519 site CA (10 yr) and
three leaf certs (3 yr): `server.pem` (SANs `DNS:localhost,
IP:127.0.0.1` + any extras, `serverAuth`), `dashboard.pem` and
`gate-client.pem` (`clientAuth`). It refuses to overwrite an existing
CA key and prints the exact flag lines each process needs.

### Proven by the E2E pass, not by inspection

`e2e_tls_stack` (ctest, 18 s) re-runs the entire 4.9 scenario — real
server, two sims, dashboard, allowlist CRUD, physical gate travel —
with every channel under mTLS on an ephemeral throwaway PKI. Then the
enforcement check: two *intruder* sims join, one speaking plaintext
and one speaking TLS without a client cert. The pass asserts neither
ever reaches the dashboard's gate map. Encryption you can demo is
nice; **exclusion you can demo is the point**. Suite total: **132**.

### Deployment plumbing

Both systemd units append an unbraced `$TLS_EXTRA_ARGS` (systemd
word-splits it, so one env var carries the whole flag set), with
production-ready commented examples in the `/etc/gate/*.env`
templates. The nginx OTA site gains a TLS mirror block on 8444 —
commented until the site PKI is installed, because `nginx -t` fails
hard on a missing certificate and a fresh install shouldn't be broken
by a block it can't satisfy yet.

Next: **Phase 4.10.2 — JWT admin authentication** (ADR-003): the
dashboard's admin surface stops trusting anyone who can reach the
socket.

---

## Phase 4.10.2 — JWT admin authentication (latest)

The dashboard's admin surface — allowlist CRUD (resident PII) and
gate commands (things that physically move) — now wants a token.
Monitoring stays open: the guard-booth view of `/api/health`,
`/api/status`, and the event WebSocket must survive an expired admin
session.

### Two mechanisms, one module

`gate_dash_auth` (jwt-cpp + OpenSSL, no Drogon — fully unit-testable):

- **Credential**: PBKDF2-HMAC-SHA256, stored as
  `pbkdf2-sha256$<iter>$<salt>$<hash>` and checked with a
  constant-time compare. `scripts/gen-admin-hash.sh` mints it (never
  echoes, never lands in argv or history). Any malformed stored hash
  **fails closed** — seven rejection paths pinned by tests.
- **Session**: HS256 JWT from `POST /api/auth/login`
  (`{"username","password"}` → `{"token","user","expiresInMin"}`).
  The signing secret is random per process by default — sessions die
  with the backend, the safe single-instance posture —
  `--jwt-secret-file` pins it for restart-surviving sessions.

Posture mirrors 4.10.1's ladder exactly: no `--admin-password-hash` =
auth disabled for dev/tests, announced loudly at startup; an
unreadable secret file refuses to start rather than degrading.

### The filter, and the linker trap it stepped into

A Drogon `HttpFilter` guards the protected routes, attached per-route
rather than globally. It promptly reproduced the Phase 4.7 lesson in
a sharper form: Drogon instantiates filters *reflectively* through
`DrObject<T>::alloc_`, a class-template static the compiler only
emits in a translation unit that odr-uses the class. Controllers
odr-use themselves via `METHOD_LIST`'s route registration — a filter
is referenced by name alone, so the whole class was silently dropped
and route setup failed at runtime with `middleware … not found`. One
deliberate `AuthFilter::classTypeName()` reference forces the
emission; the comment above it explains why it must never be
"cleaned up".

### Proven end-to-end

`e2e_auth_stack` runs the full 4.9 scenario with auth on: admin
routes answer 401 bare, a wrong password answers 401, login mints a
JWT (the hash is minted by *python's* `hashlib.pbkdf2_hmac` and
parsed by the *C++ OpenSSL* side — a free cross-implementation
check), and the CRUD + gate-travel scenarios then run fully
authorized while monitoring stays open. The SPA grew the browser
half: a sessionStorage token, `Authorization: Bearer` on every call,
and a login modal that any 401 pops. Suite total: **142**.

Deployment: `$AUTH_EXTRA_ARGS` joins the dashboard unit, with the
production example (hash + pinned secret file) commented in the env
template.

Next: **Phase 4.10.3 — firmware TLS**: the ESP32's gRPC channel
(nghttp2 → esp-tls) and HTTPS OTA adopt the site PKI the host side
already enforces.

---

## Repository layout

```
gate-automation/
├── docs/                  # ADRs, diagrams, hardware build guides, env audit
│   ├── decisions/         # 12 ADRs (ADR-000 through ADR-011)
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
├── firmware/              # ✅ Phase 4.4-4.5 — ESP-IDF field controller firmware (ESP32-S3)
│   ├── main/              # ✅ app_main — boot banner, ethernet up, GateController start
│   ├── components/
│   │   ├── gate_drivers/  # ✅ Phase 4.4.2-4.4.4 — relay/limit/eth/safety/LED drivers
│   │   ├── gate_state_machine/ # ✅ Phase 4.5.1 — pure C++20 transition logic (host-tested)
│   │   ├── gate_control/  # ✅ Phase 4.5.2 — event pump task, timers, action dispatch
│   │   ├── gate_rpc/      # ✅ Phase 4.5.3/4 — gRPC Control client (nanopb + nghttp2 h2c)
│   │   └── gate_ota/      # ✅ Phase 4.5.5 — self-hosted OTA (manifest + ed25519, ADR-009)
│   ├── host/              # Host-side static-lib mirror so Catch2 links the state machine
│   ├── partitions.csv     # Two-OTA 4 MB layout (nvs, otadata, ota_0/1, spiffs)
│   └── sdkconfig.defaults # Compile-time pinning (target=esp32s3, freertos, OTA)
├── simulation/            # ✅ Phase 4.6 — gate-sim + gate-mock-server (GPU-free demo stack)
├── dashboard/             # 🔵 Phase 4.7 — Drogon backend + SvelteKit frontend
│   ├── backend/           # ✅ 4.7.1-3 — gate-dashboard: Drogon ↔ gRPC bridge
│   └── frontend/          # ✅ 4.7.4-5 — SvelteKit SPA: monitor + allowlist + controls
├── deployment/            # ✅ Phase 4.8 — units, installer, nginx OTA site, topology docs
├── tests/                 # Catch2 unit + contract tests
│   ├── proto/             # ✅ Phase 4.1 — proto contract tests
│   ├── inference/         # ✅ Phase 4.2.6 — host-side algorithm tests
│   ├── auth/              # ✅ Phase 4.3.1 — allowlist store tests
│   ├── fusion/            # ✅ Phase 4.3.2 — fusion engine tests
│   ├── dash/              # ✅ Phase 4.3.3 — broadcaster fan-out tests
│   ├── rpc/               # ✅ Phase 4.3.4/5 — service handler + lifecycle tests
│   ├── integration/       # ✅ Phase 4.3.6 — end-to-end gRPC roundtrip tests
│   ├── state_machine/     # ✅ Phase 4.5.1 — gate state machine tests
│   ├── rpc_framing/       # ✅ Phase 4.5.3/4 — framing codec + command tracker tests
│   ├── ota_manifest/      # ✅ Phase 4.5.5 — OTA manifest rule tests
│   └── firmware_integration/ # ✅ Phase 4.5.6 — composed-module gate scenarios
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
