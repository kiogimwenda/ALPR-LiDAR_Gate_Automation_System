# ADR-002: RPC Mechanism — gRPC

## Status
Accepted

## Date
2026-04-19

## Context

The field PCB communicates with the GPU inference server over LAN. We need an efficient RPC mechanism for sending camera frames and point clouds to the server and receiving inference results. Latency budget: <150ms end-to-end, of which RPC transport should consume <10ms on LAN.

### Options Evaluated

1. **gRPC** (Google, CNCF)
   - HTTP/2 transport with Protobuf serialization
   - Streaming support (server-side, client-side, bidirectional)
   - Mature C++ implementation (`grpc++`)
   - Built-in deadline/timeout, retry, load balancing
   - Code generation from `.proto` files
   - Large ecosystem, extensive documentation
   - vcpkg port available

2. **Cap'n Proto** (Sandstorm.io)
   - Zero-copy serialization — no encode/decode step
   - Lower latency than Protobuf for large messages
   - Smaller ecosystem, fewer maintainers
   - RPC layer is less mature than gRPC
   - vcpkg port available but less actively maintained

3. **Raw Protobuf over TCP**
   - Maximum control over framing and transport
   - No framework overhead
   - Must implement connection management, retries, health checks manually
   - High maintenance burden for no compelling benefit

### Ranking

**Option 1 (gRPC) is best** because:
- The LAN latency advantage of Cap'n Proto's zero-copy (~microseconds) is negligible compared to the inference time (~50-100ms)
- gRPC's streaming support is valuable for pushing telemetry and dashboard events
- The ecosystem and tooling (grpcurl, reflection, health checking) accelerate development
- Better long-term maintainability with a CNCF-backed project

**Option 2 (Cap'n Proto) is the fallback** if gRPC's HTTP/2 overhead proves problematic on resource-constrained hardware — unlikely on any modern MCU with Ethernet.

## Decision

Use **gRPC** with Protobuf for all PCB ↔ server communication. Define service contracts in `shared/proto/`.

## Consequences

- `shared/proto/` contains `.proto` files as the single source of truth for the RPC API
- Both server and firmware link against `grpc++` and generated stubs
- Bidirectional streaming used for telemetry (PCB → server → dashboard)
- gRPC health checking protocol implemented for monitoring
