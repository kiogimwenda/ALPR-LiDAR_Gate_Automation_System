# shared/proto — gRPC Wire Contract

`gate_service.proto` is the single source of truth for every byte that crosses
a process boundary in this system. Firmware, server, dashboard, and simulation
all generate stubs from this file.

## Package & versioning

- **Package:** `gate.v1`
- **Syntax:** proto3
- **Wire stability:** Field numbers and enum values listed in
  `tests/proto/validate_descriptor.py` are frozen for the v1 protocol. Any
  change that moves a number or removes a value is a breaking change, requires
  a `gate.v2` package, and must be approved via ADR.

## Service surface

| Service | Caller | Purpose |
|---|---|---|
| `FieldControllerService.Control` | Firmware ↔ server | Long-lived bidi stream: telemetry up, commands down, acks/faults multiplexed. |
| `FieldControllerService.DeliverOta` | Firmware → server | Stream firmware image chunks during OTA. |
| `FieldControllerService.ReportOtaProgress` | Firmware → server | Progress events while the firmware applies an update. |
| `FieldControllerService.SubmitDetection` | Server-internal / sim | Push a `DetectionFrame` and receive an `AuthDecision`. |
| `DashboardService.Subscribe` | Dashboard backend | Server-streaming `DashboardEvent` feed for the web UI. |
| `DashboardService.IssueCommand` | Dashboard backend | Manual gate open/close/lockdown. |
| `DashboardService.Authorize` | Dashboard backend | Sync authorize call (used for replay & test harness). |
| `AdminService.{Upsert,List,Delete}Allowlist` | Dashboard backend | CRUD over the per-site allowlist. |

## Message budget

Telemetry messages must encode in under **256 bytes** (firmware bandwidth
budget on cellular fallback). The contract test
`tests/proto/proto_contract_test.cpp::Telemetry stays under 256-byte firmware
budget` enforces this at build time.

## Generating stubs

### Server / dashboard / sim (full gRPC, host build)

The `shared/proto/CMakeLists.txt` module generates and links a static
`gate_proto` target from the project build. Consumers only need
`target_link_libraries(<target> PRIVATE gate_proto)`.

### Firmware (lite runtime, ESP-IDF)

The ESP32-S3 firmware uses `protobuf-c` (lite, no exceptions) via the
`firmware/components/gate_proto` ESP-IDF component. A future change in this
phase will add the codegen step to that component's CMake.

## Validation

Two layers:

1. **`validate_descriptor.py`** — runs in CI on every push that touches
   `shared/proto/`. Checks that all expected messages, enums, services and
   streaming kinds are present with their stable field numbers. Requires only
   `protoc` + `python3-protobuf` (no vcpkg toolchain).
2. **`proto_contract_test.cpp`** — Catch2 round-trip tests, compiled and run
   as part of the C++ test suite once the full vcpkg toolchain is bootstrapped.

## Local quick check

```bash
# From repo root
protoc --proto_path=shared/proto \
       --descriptor_set_out=/tmp/gate.desc \
       shared/proto/gate_service.proto

python3 tests/proto/validate_descriptor.py
```

## Related ADRs

- [ADR-002: RPC mechanism (gRPC)](../../docs/decisions/ADR-002-rpc-mechanism.md)
- [ADR-009: OTA strategy](../../docs/decisions/ADR-009-ota-strategy.md)
