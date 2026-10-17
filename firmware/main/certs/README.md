# Site PKI provisioning (Phase 4.10.3)

With `CONFIG_GATE_TLS_ENABLE=y` the build embeds three PEM files from
this directory into the firmware image:

| File | Role |
|---|---|
| `site_ca.pem` | Verifies the gate-server's gRPC listener **and** https OTA URLs |
| `gate.pem` | This gate's client certificate (the server runs `--require-client-cert`) |
| `gate.key` | The matching private key |

Mint them from the repo root with the same tool the host stack uses:

```sh
./scripts/gen-tls-certs.sh /path/to/site-pki
cp /path/to/site-pki/ca.pem          firmware/main/certs/site_ca.pem
cp /path/to/site-pki/gate-client.pem firmware/main/certs/gate.pem
cp /path/to/site-pki/gate-client.key firmware/main/certs/gate.key
```

Then point `CONFIG_GATE_OTA_MANIFEST_URL` at the nginx TLS mirror
(`https://…:8444/firmware/manifest.json`).

Certificates and keys are **per-site secrets** — they are gitignored
here and must never be committed. A missing file with TLS enabled
fails the build (`EMBED_TXTFILES`), which is the intended fail-closed
behaviour: a gate never ships half-provisioned.

Production: give each gate its own leaf instead of the shared
`gate-client.pem`:

```sh
./scripts/issue-gate-cert.sh /path/to/site-pki gate-01
cp /path/to/site-pki/gate-01.pem firmware/main/certs/gate.pem
cp /path/to/site-pki/gate-01.key firmware/main/certs/gate.key
```

Its CN is `gate-field-gate-01`; gate-server then accepts this board's
Control stream only as `gate-01` (and never for admin RPCs), so a
compromised controller is one certificate to retire. The PKI is ECDSA
P-256 — the ESP32-S3's mbedTLS cannot use Ed25519 certificates.
