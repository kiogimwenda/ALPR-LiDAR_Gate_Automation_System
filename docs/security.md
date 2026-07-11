# Security posture (Phase 4.10)

What protects what, where the keys live, and which guarantees hold
even when something else fails. Every mechanism below is enforced by
a test the suite runs on every commit — the E2E passes don't just
demo encryption working, they demo **exclusion**: peers with the
wrong credentials provably never reach the stack.

## The layers

| Surface | Mechanism | Introduced |
|---|---|---|
| gRPC fabric (server ↔ dashboard, sims, firmware) | mTLS against the site CA; server refuses to start on unreadable certs | 4.10.1 / 4.10.3 |
| Dashboard admin API (allowlist, gate commands) | PBKDF2 credential → HS256 JWT; per-route filter | 4.10.2 |
| OTA images | SHA-256 readback + ed25519 signature over the digest (transport-independent) | 4.5.5 / ADR-009 |
| OTA transport | https against the site CA (nginx 8444 mirror) | 4.10.3 |
| systemd units | NoNewPrivileges, ProtectSystem=strict, PrivateTmp, dedicated user | 4.8 |

## The site PKI

`scripts/gen-tls-certs.sh` mints an ed25519 CA (10 yr) and three
leaf certs (3 yr): `server.pem` (SAN-bearing, serverAuth),
`dashboard.pem` and `gate-client.pem` (clientAuth). One CA per site;
the CA key never leaves the machine that minted it. Fleet note: issue
one leaf per gate (SAN = gate id) so a compromised controller can be
revoked without re-keying the site.

The ladder is deliberate, and identical on every process:

1. nothing configured → plaintext, **loudly logged** — dev/test only;
2. server cert + key → TLS;
3. \+ CA → client certs verified when presented;
4. \+ `--require-client-cert` → mTLS, no valid cert = no connection.

Two postures are non-negotiable everywhere: an *unreadable* secret
(PEM, JWT secret file) **refuses to start** — asked-for security
never silently degrades; and security-off is legal but announces
itself at startup, so a misconfigured production box is loud in the
journal.

## Admin authentication

`pbkdf2-sha256$iter$salt$hash` (mint with `scripts/gen-admin-hash.sh`;
never echoed, never in argv), constant-time compare, malformed hashes
fail closed. Login mints an HS256 JWT (default TTL 8 h); the signing
secret is per-process random unless pinned. The filter protects what
*changes* the site or reads resident PII; monitoring endpoints stay
open so the guard-booth view survives an expired session.

## Keys that must stay offline

- **OTA signing private key** (ADR-009): lives on the release
  machine only — never on a gate, never on the update server. The
  gates hold only the public key (`CONFIG_GATE_OTA_PUBKEY`).
- **Site CA key**: needed only to issue certs; not required at
  runtime by any process.

Per-site materials (`/etc/gate/tls`, `firmware/main/certs/`,
`/etc/gate/jwt.secret`) are gitignored everywhere they could appear;
the firmware build **fails** if TLS is enabled and a PEM is missing —
a gate never ships half-provisioned.

## What the tests enforce

| ctest entry | Asserts |
|---|---|
| `e2e_tls_stack` | full scenario under mTLS; a plaintext intruder and a certless-TLS intruder never appear in the dashboard's gate map |
| `e2e_auth_stack` | admin routes 401 bare and on a wrong password; a minted JWT opens them; monitoring stays open |
| `e2e_hardened_stack` | both at once — the production posture end to end |
| `test_dashboard_auth` | every credential/token rejection path, incl. expiry, tamper, foreign secret, malformed stored hash |

## Known residual items

- The GPU inference path (camera → TensorRT → `SubmitDetection`) is
  not yet covered by E2E and its capture credentials (RTSP) are
  out of scope until that lands.
- Firmware TLS is compile-verified; on-hardware handshake validation
  is bundled with the 4.5 bench items.
- JWT revocation is expiry-only (no server-side session store) —
  acceptable at the current single-admin scale; revisit if the
  operator model grows.
