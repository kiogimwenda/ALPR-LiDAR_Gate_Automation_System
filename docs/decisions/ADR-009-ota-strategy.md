# ADR-009: OTA Update Strategy — Self-Hosted with ed25519 Signing

## Status
Accepted

## Date
2026-04-19

## Context

Both the field PCB firmware and the GPU server binaries need over-the-air updates. Requirements:
- Signed updates (prevent unauthorized firmware)
- Atomic installation (no partial updates that brick the device)
- Rollback on failure (automatic revert if new version crashes)
- Works over LAN (no cloud dependency)
- Minimal infrastructure (single-site residential estate)

### Options Evaluated

1. **Self-hosted HTTPS update server**
   - Simple: static file server + signed JSON manifest
   - Update bundle: compressed binary + ed25519 signature + SHA-256 checksum
   - Atomic swap via A/B partitions (ESP32-S3 OTA) or rename-based swap (server)
   - No external dependencies
   - Full control over update flow

2. **Mender** (Northern.tech)
   - Enterprise OTA platform, open-source server
   - A/B partition updates, rollback, fleet management
   - Heavy: requires Docker, MongoDB, MinIO on the server side
   - Overkill for a single-site deployment
   - ~KES 0 (open source) but significant infrastructure cost

3. **RAUC** (Pengutronix)
   - Linux-focused, A/B rootfs updates
   - Lightweight, well-suited for embedded Linux
   - Not applicable to ESP32-S3 (bare-metal / FreeRTOS)

4. **SWUpdate** (Stefano Babic)
   - Similar to RAUC, Linux-focused
   - Lua scripting for update logic
   - Same limitation: not applicable to ESP32-S3

### Ranking

**Option 1 (Self-hosted) is best** because:
- ESP32-S3 has built-in OTA support in ESP-IDF (A/B partitions, rollback)
- We only need to add signing verification (ed25519) and manifest checking
- Server-side updates are a simple binary swap with systemd restart
- No external infrastructure or Docker required
- Single-site deployment doesn't justify Mender's fleet management overhead

**Option 2 (Mender) is the future option** if the system scales to multiple sites with fleet management needs.

## Decision

Self-hosted OTA update server:
- **Signing:** ed25519 keypair (private key offline, public key embedded in firmware/server)
- **Transport:** HTTPS (self-signed TLS cert on LAN)
- **Manifest:** `manifest.json` with version, SHA-256, signature, release notes
- **PCB updates:** ESP-IDF OTA API (A/B partition, automatic rollback on boot failure)
- **Server updates:** Binary swap + systemd restart, rollback via saved previous binary
- **Update check interval:** Configurable, default every 6 hours

## Consequences

- OTA signing keypair generated during deployment setup (ed25519)
- Private key stored offline (never on the device or update server)
- Update server is a simple static file server (Drogon endpoint or nginx)
- ESP-IDF `esp_ota_ops` API handles partition management
- Self-test after update: if firmware crashes 3 times in a row, ESP-IDF rolls back automatically
- Server self-test: health check endpoint called after restart; if unhealthy, systemd restarts with previous binary via `ExecStartPre` check
