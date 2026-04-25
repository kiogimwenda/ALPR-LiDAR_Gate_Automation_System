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
| **4.1** | **gRPC wire contract (`shared/proto`)** | ✅ **Complete — see "Latest accomplishment" below** |
| 4.2 | Server inference (TensorRT engines for YOLOv9 + PaddleOCR) | ⏳ Next |
| 4.3 | Server RPC + fusion engine | ⏳ Pending |
| 4.4 | Firmware drivers (W5500, relays, sensors) | ⏳ Pending |
| 4.5 | Firmware app (state machine, gRPC client, OTA) | ⏳ Pending |
| 4.6 | Simulation harness | ⏳ Pending |
| 4.7 | Dashboard backend + frontend | ⏳ Pending |
| 4.8 | Deployment scripts (systemd, install) | ⏳ Pending |
| 4.9 | End-to-end integration tests | ⏳ Pending |

---

## Latest accomplishment — Phase 4.1: gRPC wire contract

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
├── server/                # ⏳ Phase 4.2 — ALPR + LiDAR inference + fusion
├── firmware/              # ⏳ Phase 4.4 — ESP-IDF field controller firmware
├── simulation/            # ⏳ Phase 4.6 — virtual gate harness
├── dashboard/             # ⏳ Phase 4.7 — Drogon backend + SvelteKit frontend
├── deployment/            # ⏳ Phase 4.8 — systemd units + install scripts
├── tests/                 # Catch2 unit + contract tests
│   └── proto/             # ✅ Phase 4.1 — proto contract tests
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
