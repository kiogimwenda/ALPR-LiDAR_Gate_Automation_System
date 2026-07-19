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

Production note: for a real fleet, issue one leaf per gate (SAN =
gate id) instead of sharing `gate-client.pem`, so a compromised
controller can be revoked without re-keying the site.
