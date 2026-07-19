# ADR-011: Firmware gRPC Transport — nanopb + nghttp2 (h2c)

## Status
Accepted

## Date
2026-07-04

## Context

ADR-002 chose gRPC for all PCB ↔ server communication and stated, as a
consequence, that "both server and firmware link against `grpc++` and
generated stubs". For the server that consequence held (Phase 4.3.4).
For the firmware it cannot: `grpc++` and its dependency tree (abseil,
re2, c-ares, full protobuf runtime) assume a POSIX host, weigh tens of
megabytes, and have no ESP-IDF port. The ESP32-S3 field controller has
4 MB flash (≈1.5 MB per OTA slot) and 512 KB SRAM. The wire protocol
stays; the client library needs an embedded-grade implementation.

gRPC over the wire is three layers, each of which has a mature
embedded-grade implementation:

1. **Protobuf messages** → **nanopb** (0.4.x): plain-C runtime,
   ~10 KB code, statically-allocated message structs sized by a
   `.options` file at generation time. Available on the ESP component
   registry (`livekit/nanopb`, tracking upstream 0.4.9).
2. **gRPC message framing** → a 5-byte prefix (1-byte compressed flag +
   4-byte big-endian length) per message. Trivial to implement; pure
   logic, host-testable.
3. **HTTP/2 transport** → **nghttp2**, the reference C implementation
   (`espressif/nghttp` on the registry). The server listens with
   `InsecureServerCredentials`, i.e. cleartext HTTP/2 (h2c) — a gRPC
   client on an insecure port sends the HTTP/2 client preface directly,
   no TLS and no HTTP/1.1 Upgrade dance.

### Options Evaluated

1. **nanopb + nghttp2 over a plain BSD socket** (chosen)
   - All three layers under our control; ~10–60 KB flash total
   - `espressif/sh2lib` was considered for layer 3 but is hardwired to
     esp-tls with ALPN `h2` — it cannot speak h2c. A thin session
     wrapper over `nghttp2` + `socket()`/`poll()` is ~300 lines and
     drops the esp-tls dependency entirely.
   - Framing and message layers are pure logic → host-unit-testable
     with the same mirror pattern as `gate_state_machine`

2. **Sidecar protocol translation** (WebSocket/raw-TCP bridge on the
   server, custom protocol to the firmware)
   - Avoids HTTP/2 on the MCU
   - Violates ADR-002's single-contract goal: a second wire protocol,
     a second server endpoint, double the contract-drift surface

3. **grpc-web via an Envoy proxy**
   - Still needs an HTTP client + proxy infrastructure on a LAN
     deployment that has neither; adds an Envoy instance to operate

### On TLS

h2c matches the server's current `InsecureServerCredentials` listener
on an isolated field LAN. When transport security lands (mTLS is the
obvious end state for gate actuators), the socket layer swaps to
esp-tls without touching framing or messages — and at that point
`sh2lib` becomes viable again. Tracked for a later phase; out of scope
here.

## Decision

Firmware speaks real gRPC using **nanopb** for messages, a first-party
**framing codec**, and **nghttp2 over a plain socket** for h2c
transport. Generated nanopb sources are committed (regenerated via
`scripts/gen-nanopb.sh`, generator version pinned to the runtime
component's version) so CI and clean checkouts build without a Python
toolchain.

## Consequences

- ADR-002's "firmware links `grpc++`" consequence is superseded by
  this ADR; everything else in ADR-002 stands.
- `shared/proto/gate_service.proto` remains the single source of truth;
  nanopb generates from the same file the server's protoc pass uses.
- Static message sizing lives in `shared/proto/gate_service.options`;
  growing a string field on the wire may require a matching
  `max_size` bump there.
- The firmware's gRPC feature set is only what we implement: no
  compression (flag always 0), no retries beyond our reconnect loop,
  deadlines via socket timeouts. Acceptable — the Control stream is a
  long-lived session on a LAN, not a request/response fan-out.
