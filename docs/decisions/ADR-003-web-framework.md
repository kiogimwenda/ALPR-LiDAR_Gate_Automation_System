# ADR-003: Web Framework for Dashboard Backend — Drogon

## Status
Accepted

## Date
2026-04-19

## Context

The dashboard backend is a C++ HTTP/WebSocket server that exposes REST endpoints for event queries, system status, and gate override commands, plus a WebSocket endpoint for real-time event streaming. It must handle ~10 concurrent connections (guards, admins) with low latency.

### Options Evaluated

1. **Crow** (CrowCpp)
   - Header-only, minimal setup
   - Flask-like API, easy to learn
   - Single-threaded by default (can enable multithreading)
   - Limited middleware support
   - Less active maintenance recently

2. **Drogon** (drogon-web)
   - High-performance async framework built on `trantor` (event loop library)
   - First-class WebSocket support
   - Built-in ORM (not needed — we use SQLite directly)
   - Non-blocking I/O throughout
   - Active development, good documentation
   - vcpkg port available
   - Supports HTTP/1.1 and HTTP/2

3. **Oat++** (oatpp)
   - Object-oriented API design
   - Built-in Swagger/OpenAPI generation
   - WebSocket support
   - More boilerplate than Drogon
   - Smaller community

### Ranking

**Option 2 (Drogon) is best** because:
- Native async WebSocket support is critical for real-time dashboard updates
- Highest performance of the three (benchmarked in TechEmpower)
- Active maintenance and responsive community
- Non-blocking design pairs well with our latency-sensitive pipeline
- vcpkg port simplifies integration

**Option 1 (Crow) is the fallback** if Drogon proves too heavy — but Crow's WebSocket support is weaker.

## Decision

Use **Drogon** for the dashboard backend.

## Consequences

- Dashboard backend links against `drogon` (installed via vcpkg)
- REST endpoints for: events, status, allowlist/blocklist CRUD, gate override
- WebSocket endpoint for real-time event stream to frontend
- Drogon's built-in JSON support (via `nlohmann-json` or `jsoncpp`) used for API responses
