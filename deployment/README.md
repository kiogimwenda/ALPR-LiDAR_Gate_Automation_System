# Deployment

Single-site topology (ADR-009: minimal infrastructure, no cloud):

```
GPU host (Ubuntu server)                     Field
┌────────────────────────────────┐           ┌──────────────────────┐
│ gate-server      :50051 (gRPC) │◀──LAN────▶│ ESP32-S3 controller  │
│ gate-dashboard   :8080  (HTTP) │           │ (or gate-sim fleet)  │
│ nginx gate-ota   :8081  (HTTP) │           └──────────────────────┘
└────────────────────────────────┘
        ▲ browser (dashboard SPA, same-origin from :8080)
```

## Install / upgrade (GPU host)

```bash
cmake --preset release && cmake --build --preset release
(cd dashboard/frontend && npm ci && npm run build)
sudo deployment/install-server.sh --build build/release --www dashboard/frontend/build
```

The installer creates the `gate` service account, installs binaries to
`/opt/gate/bin` (keeping the previous as `*.prev` for manual
rollback), the SPA to `/opt/gate/www`, default configs to
`/etc/gate/*.env` (first run only — upgrades never clobber edits), and
enables both units. Runtime knobs live in the env files; the units
never need editing.

## OTA release flow (ADR-009)

One-time, on an **offline** signing machine:

```bash
scripts/gen-ota-keys.sh ota-keys
# paste ota-keys/ota-pubkey.hex into firmware menuconfig → GATE_OTA_PUBKEY
```

Per release:

```bash
idf.py build          # produces build/gate_firmware.bin
scripts/sign-ota-manifest.sh \
    --bin firmware/build/gate_firmware.bin \
    --version 0.2.0 \
    --url http://<gpu-host>:8081/firmware/gate_firmware.bin \
    --key ota-keys/ota-signing.key --key-id deploy-2026
scp firmware/build/gate_firmware.bin manifest.json <gpu-host>:/srv/gate/firmware/
```

The signature is ed25519 over the raw 32-byte SHA-256 of the image —
the same digest the firmware recomputes from a flash readback — and
the script self-verifies before writing the manifest. A BEGIN_OTA
command from the dashboard then drives the whole pipeline
(fetch → validate → stream → verify → A/B flip → rollback-guarded
boot).

## Deliberately not here (Phase 4.10 — security hardening)

TLS/mTLS on the gRPC and OTA channels, JWT on the admin API, and the
nginx TLS server block. Today's posture per the ADRs: an isolated
field LAN, with update integrity carried by signatures rather than
transport. The hardening pass upgrades transport everywhere at once —
server credentials, dashboard channel, firmware esp-tls, gate-sim —
so the system is never half-TLS.
