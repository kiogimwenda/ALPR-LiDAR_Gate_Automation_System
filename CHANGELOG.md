# Changelog

All notable changes to the ALPR + LiDAR Automated Vehicle Gate Control
System. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions are tagged `vX.Y.Z-phaseN` matching the
[development timeline](README.md#development-timeline). The full build
story — every sub-milestone with design reasoning — lives in the
README's chronological log; this file is the executive summary.

## [Unreleased]

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
