# Operations Runbook

Day-2 operations for a deployed site: what to check, what to rotate,
and what to do when something misbehaves at 2 a.m. Site bring-up lives
in the [commissioning checklist](commissioning.md) (install mechanics:
[`deployment/README.md`](../deployment/README.md));
first-time hardware validation lives in the
[bench validation plan](hardware/11-bench-validation-plan.md); the
security model behind every credential mentioned here is
[`docs/security.md`](security.md).

Conventions: `<gpu-host>` is the GPU server's LAN address, `<gate-id>`
is the gate's configured id (`CONFIG_GATE_ID`, e.g. `gate-01`). All
`curl` examples run from any machine on the field LAN; add
`https://` and the site CA once the TLS posture is on.

---

## 1. The moving parts

| Process | systemd unit | Host | Port(s) | Config | Logs |
|---|---|---|---|---|---|
| `gate-server` (gRPC + fusion + allowlist) | `gate-server.service` | GPU host | 50051 gRPC | `/etc/gate/gate-server.env` | `journalctl -u gate-server` |
| `gate-dashboard` (REST/WS + SPA) | `gate-dashboard.service` | GPU host | 8080 HTTP/WS | `/etc/gate/gate-dashboard.env` | `journalctl -u gate-dashboard` |
| `gate-vision` (camera → SubmitDetection) | `gate-vision.service` | GPU host | none (client of 50051) | `/etc/gate/gate-vision.env` | `journalctl -u gate-vision` |
| OTA file server | nginx (site `gate-ota`) | GPU host | 8081 http, 8444 https mirror | `/etc/nginx/sites-available/gate-ota` | `journalctl -u nginx` |
| Field controller firmware | — (ESP32-S3) | at the gate | dials `<gpu-host>:50051` | Kconfig (build-time) | serial: `idf.py -p /dev/ttyACM0 monitor` |

Fixed paths on the GPU host:

- Binaries: `/opt/gate/bin/{gate-server,gate-dashboard,gate-vision}`,
  each with a `.prev` sibling kept by the installer for rollback.
- SPA: `/opt/gate/www` (dashboard runs API-only without it).
- Allowlist/audit DB: `/var/lib/gate/gate.db` (SQLite, WAL mode).
- Site PKI: `/etc/gate/tls` (`root:gate`, keys mode 0640).
- JWT secret (if pinned): `/etc/gate/jwt.secret`.
- OTA publish directory: `/srv/gate/firmware` (manifest + images).

All three units carry a crash-loop brake
(`StartLimitIntervalSec=60`, `StartLimitBurst=5`): five failures in a
minute and systemd stops restarting. That is deliberate — see
[§7](#7-upgrades) for the `.prev` rollback it protects.

`gate-vision` is installed but **not** enabled by the installer: it
needs `SOURCE_ARGS` configured in its env (camera + engines, or a
scenario file) before it can start.

---

## 2. Routine health checks

### The two endpoints

Both are unauthenticated by design — the guard-booth view must survive
an expired admin session:

```bash
curl -s http://<gpu-host>:8080/api/health
# good: {"status":"ok","upstream":true,"upstreamAddr":"127.0.0.1:50051","siteId":"site-01"}

curl -s http://<gpu-host>:8080/api/status
# good: "streamConnected":true, "upstream":true, and every deployed
# gate present under "gates" with a recent telemetry snapshot and its
# fwVersion.
```

Reading `/api/health`: `status` is the dashboard backend itself;
`upstream` is the gRPC channel to gate-server. `status:ok` with
`upstream:false` means the backend is fine and gate-server is down or
unreachable — go to [§8.2](#82-server-down).

Reading `/api/status`: a gate missing from `gates` has never connected
this server lifetime; a gate present but stale means its stream
dropped (the firmware reconnects on its own — see §8.2).
`streamConnected` is the backend's own event subscription to
gate-server.

### Journal postures — what "good" looks like

Every process announces a degraded security posture loudly at startup
(the ladder in [`docs/security.md`](security.md)). On a production
box, these greps must come back **empty**:

```bash
journalctl -u gate-server -b   | grep -i "PLAINTEXT"
journalctl -u gate-dashboard -b | grep -i "auth: DISABLED"
journalctl -u gate-vision -b   | grep -i "PLAINTEXT"
```

The exact lines you are ruling out (and their healthy counterparts):

| Unit | Bad (dev posture) | Good |
|---|---|---|
| gate-server | `transport: PLAINTEXT — fine for dev/tests; production wants --tls-cert/--tls-key (Phase 4.10)` | `transport: mTLS — client certs required (cert=/etc/gate/tls/server.pem)` |
| gate-dashboard | `admin auth: DISABLED — allowlist + gate commands are open` | `admin auth: ON (user=admin, ttl=480min, secret=pinned)` |
| gate-vision | `submit_client: no tls_ca_path set — using PLAINTEXT channel to …` | no such line |

Also worth a glance: `systemctl status gate-server gate-dashboard
gate-vision` (active, no recent restarts), and on the gate side a
5-second `gate-ctrl: heartbeat: state=… beam=… limits[…]` cadence on
the serial console (exact lines in
[`firmware/README.md`](../firmware/README.md)).

Note the two failure modes are asymmetric on purpose: a *missing*
credential runs but shouts; an *unreadable* one (bad PEM path, empty
`jwt.secret`) **refuses to start** — asked-for security never silently
degrades. A unit stuck in a restart loop right after a config change
usually means the latter; the journal names the file.

---

## 3. Allowlist administration

The allowlist is resident PII (names, units, plates), so all three
verbs sit behind the admin JWT. Log in first:

```bash
TOKEN=$(curl -s -X POST http://<gpu-host>:8080/api/auth/login \
    -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"…"}' | jq -r .token)
```

The token is good for `expiresInMin` minutes (default 480 — 8 h;
`--token-ttl-min` changes it). A 503 from `/api/auth/login` means
auth is not configured on this backend (dev posture) — see the caveat
below. Wrong user and wrong password are the same 401 by design.

```bash
# List (paged):
curl -s -H "Authorization: Bearer $TOKEN" \
    'http://<gpu-host>:8080/api/allowlist?pageSize=100'
# follow "nextPageToken" with &pageToken=…

# Add / update one plate (upsert — same call updates an existing entry):
curl -s -X POST -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    http://<gpu-host>:8080/api/allowlist \
    -d '{"plate":"KDA123A","ownerName":"J. Mwangi","ownerUnit":"B-12","notes":"resident"}'

# Batch: same endpoint, body {"entries":[{…},{…}]}

# Remove:
curl -s -X DELETE -H "Authorization: Bearer $TOKEN" \
    http://<gpu-host>:8080/api/allowlist/KDA123A
```

Optional entry fields: `allowedClasses` (array of vehicle-class
names), `timeWindows` (array of `{"startMinute":…,"endMinute":…,
"daysMask":…}`, minutes 0–1439), `validFromMs` / `validUntilMs`
(epoch milliseconds). Validation errors come back as 400s naming the
bad field.

**Dev-posture caveat:** when `AUTH_EXTRA_ARGS` is empty in
`gate-dashboard.env`, the auth filter passes everything through —
the allowlist and gate commands are open to anyone on the LAN, and
`/api/auth/login` answers 503 rather than minting tokens nothing will
check. If your login unexpectedly 503s on a production box, the
backend was started without its credential: fix the env, restart, and
treat it as an incident (the startup warning in the journal will show
how long it ran open).

---

## 4. Credential rotation

The custody rules come first, because they decide *where* each step
runs ([`docs/security.md`](security.md), "Keys that must stay
offline"):

- The **OTA signing key** (`ota-signing.key`) lives only on the
  offline release machine — never on a gate, never on the GPU host.
- The **site CA key** (`ca.key`) is needed only to issue certs; no
  runtime process reads it. Keep it off the server.
- What *is* on the server: leaf certs + keys under `/etc/gate/tls`,
  the admin password **hash** in `gate-dashboard.env`, and (if
  pinned) `/etc/gate/jwt.secret`.

### 4.1 Admin password

```bash
./scripts/gen-admin-hash.sh          # prompts twice; never echoes, never in argv
# ITERATIONS=500000 ./scripts/gen-admin-hash.sh   # to raise the work factor
```

Paste the printed `pbkdf2-sha256$…` string into the
`--admin-password-hash` value inside `AUTH_EXTRA_ARGS` in
`/etc/gate/gate-dashboard.env`, then:

```bash
sudo systemctl restart gate-dashboard
journalctl -u gate-dashboard -b | grep "admin auth"   # expect: ON
```

Existing JWTs stay valid until expiry — the password gates *minting*
tokens, not verifying them. To also kill live sessions, rotate the
JWT secret in the same restart (§4.2).

### 4.2 JWT signing secret

`/etc/gate/jwt.secret` is any single line of high-entropy text, mode
0640 `root:gate`:

```bash
openssl rand -hex 32 | sudo tee /etc/gate/jwt.secret >/dev/null
sudo chown root:gate /etc/gate/jwt.secret && sudo chmod 0640 /etc/gate/jwt.secret
sudo systemctl restart gate-dashboard
```

Rotating it invalidates **every** outstanding session immediately —
expect the 401 storm described in [§8.4](#84-dashboard-401s-everyone);
everyone just logs in again. There is no server-side session store to
purge (revocation is expiry-only, a documented residual in
`security.md`).

If the file is configured but unreadable/empty, the backend exits
with `cannot read a JWT secret from …` rather than degrading to a
random secret. If no `--jwt-secret-file` is configured at all, the
secret is random per process — legal, but every restart logs everyone
out; production wants the pinned file.

### 4.3 TLS certificates

Lifetimes, set by [`scripts/gen-tls-certs.sh`](../scripts/gen-tls-certs.sh):
site CA 10 years, leaf certs (`server.pem`, `dashboard.pem`,
`gate-client.pem`) 3 years. Calendar the leaf expiry at install time —
an expired `server.pem` takes down the entire gRPC fabric at once:
dashboard `upstream:false`, gate-vision submit failures, every gate's
stream refused (gates keep operating locally per §8.2, but no plates
open anything).

**Leaf renewal** needs the offline CA key. `gen-tls-certs.sh` has no
leaf-only mode — it refuses to run into a directory that already
holds a `ca.key` (deliberately, so a fat-fingered re-run can't
silently re-key the site). On the machine holding the CA key, reissue
a leaf with the same openssl steps the script's `issue()` function
uses (CSR signed by `ca.pem`/`ca.key`, 1095 days, `serverAuth` +
SANs for the server leaf, `clientAuth` for the client leaves), then:

```bash
sudo install -m 0640 -o root -g gate server.pem server.key /etc/gate/tls/
sudo systemctl restart gate-server gate-dashboard gate-vision
```

What breaks meanwhile: restarting gate-server drops every gate's
control stream and the dashboard's event stream; both reattach on
their own (backoff bounds in §8.2, ≤ ~35 s). No firmware action is
needed for a leaf renewal — the gates verify against `ca.pem`, which
hasn't changed.

**Re-keying the CA** is a different animal: the firmware embeds
`ca.pem` at build time (`firmware/main/certs/`, baked in via
`EMBED_TXTFILES`). Sequence matters: build + OTA firmware that trusts
the *new* CA to every gate **before** switching the server's certs,
or you strand the fleet off the mTLS fabric with no OTA path back
(the OTA https mirror uses the same CA). Treat CA rotation as a
planned maintenance window, not a 2 a.m. task.

---

## 5. OTA release procedure

Production framing of the flow in
[`deployment/README.md`](../deployment/README.md) and
[ADR-009](decisions/ADR-009-ota-strategy.md). Integrity is carried by
the manifest — SHA-256 of the image plus an ed25519 signature over
that digest — not by the transport, which is why plain http on 8081
is acceptable and why the signing key's location is the whole
security story.

**On the offline release machine** (the only place
`ota-signing.key` exists):

```bash
idf.py build          # firmware/build/gate_firmware.bin, version bumped in
                      # firmware/components/gate_drivers/src/version.cpp
scripts/sign-ota-manifest.sh \
    --bin firmware/build/gate_firmware.bin \
    --version 0.3.0 \
    --url http://<gpu-host>:8081/firmware/gate_firmware.bin \
    --key ota-keys/ota-signing.key --key-id deploy-2026
```

(`--min-uptime` defaults to 60 s; use the `https://…:8444` URL form
once the TLS posture is on — the firmware's Kconfig mandates the
mirror when TLS is enabled.) The script self-verifies the signature
before writing `manifest.json`, so a corrupt key fails here, not in
the field.

**Publish to the GPU host:**

```bash
scp firmware/build/gate_firmware.bin manifest.json <gpu-host>:/srv/gate/firmware/
curl -sI http://<gpu-host>:8081/firmware/manifest.json   # 200 + Cache-Control: no-cache
```

**Trigger, one gate at a time** (JWT per §3):

```bash
curl -s -X POST -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    http://<gpu-host>:8080/api/gates/<gate-id>/command \
    -d '{"kind":"BEGIN_OTA"}'
```

The immediate ack confirms the gate accepted the job; progress phases
stream on `/ws/events` (`otaProgress`), and the gate's status LED
pulses for the duration. On success the gate acks completion, reboots
after 1.5 s, and its `fwVersion` in `/api/status` reports the new
version — that field is your fleet rollout tracker. Do the first gate,
verify `fwVersion` and a command round-trip, then proceed to the rest.

**Rollback story:**

- A failed or *rejected* update never touches the running slot — A/B
  partitions, verified by flash readback before the flip.
- A new image that crash-loops is reverted by the bootloader on its
  own (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`: the image boots
  `PENDING_VERIFY` and is only marked valid at steady state).
- To *deliberately* downgrade, sign the previous binary as a new
  release — the manifest validator refuses a `target_version` equal
  to the running version, so a re-serve of the old manifest is a
  no-op by design.
- Host services roll back via `.prev` binaries (§7), not OTA.

Rejections and stuck updates: [§8.5](#85-ota-stuck-or-rejected).

---

## 6. Backup and restore

### The allowlist/audit database

`gate.db` is SQLite in WAL mode, so a consistent backup while
gate-server is live is one command:

```bash
sudo -u gate sqlite3 /var/lib/gate/gate.db ".backup '/var/lib/gate/gate.db.bak'"
```

then move `gate.db.bak` off-host. Restore requires a stop (the
server holds the DB open):

```bash
sudo systemctl stop gate-server
sudo rm -f /var/lib/gate/gate.db-wal /var/lib/gate/gate.db-shm   # stale WAL sidecars
sudo install -o gate -g gate -m 0640 gate.db.bak /var/lib/gate/gate.db
sudo systemctl start gate-server
```

### Everything else worth copying

| Material | Where | Why |
|---|---|---|
| Env files | `/etc/gate/*.env` | the operator-edited runtime config; the installer never recreates edits |
| Site PKI leaves | `/etc/gate/tls` | avoids an emergency reissue after disk loss |
| Site CA key | offline machine (never the server) | without it, no leaf can ever be renewed — losing it forces a full re-key incl. firmware rebuild (§4.3) |
| OTA signing key | offline release machine | losing it forces a pubkey rotation via a *signed* transitional OTA — painful; guard it |
| JWT secret | `/etc/gate/jwt.secret` (if pinned) | restoring it keeps sessions valid across a host rebuild |
| Published firmware | `/srv/gate/firmware` | optional — reproducible from the release process |

**Not backed up by design:** a per-process JWT secret (no
`--jwt-secret-file`). It exists only in the process's memory; there
is nothing to copy, and its loss on restart merely forces re-login.

---

## 7. Upgrades

### GPU-host services

```bash
cmake --preset release && cmake --build --preset release
(cd dashboard/frontend && npm ci && npm run build)
sudo deployment/install-server.sh --build build/release --www dashboard/frontend/build
sudo systemctl restart gate-server gate-dashboard gate-vision
```

Re-running the installer is idempotent: it upgrades binaries and the
SPA, re-installs units, and **never clobbers an edited
`/etc/gate/*.env`** — your TLS/auth/source config survives every
upgrade. The flip side: if a release introduces a new env knob, the
installer won't merge it into your live file; diff yours against the
defaults in [`deployment/config/`](../deployment/config/) after
upgrading.

Each install keeps the previous binary as
`/opt/gate/bin/<name>.prev`. If the new build crash-loops (the
units' `StartLimitBurst=5` brake will have tripped):

```bash
sudo cp /opt/gate/bin/gate-server.prev /opt/gate/bin/gate-server
sudo systemctl restart gate-server
```

(Same pattern for `gate-dashboard` and `gate-vision`.) One level of
history only — a second install overwrites `.prev`.

### Firmware

Via OTA, [§5](#5-ota-release-procedure). `idf.py flash` over USB is
the bench/recovery path and needs physical access to the controller.

---

## 8. Incident playbooks

### 8.1 Gate won't move

Triage in this order — verdict, command path, hardware — using
`/api/status`, the event feed, the journal, and (last) the serial
console.

**Is the gate even connected?** `/api/status`: gate absent or stale →
this is a connectivity problem, not a gate problem; see §8.2 and the
serial console (`gate-rpc: reconnecting in …ms` means it can't reach
the server; check LAN/switch/cert validity).

**Plate read but gate closed → verdict problem.** Watch `/ws/events`
(or the dashboard UI): each detection produces a decision event with
`verdict` and `denyReason`, plus `matchedPlate`. A deny is the system
working — check the allowlist entry (§3): present? within its
`timeWindows`/`daysMask`? inside `validFromMs`/`validUntilMs`? right
vehicle class? No decision events at all → `gate-vision` is down or
sourceless: `journalctl -u gate-vision -b`.

**Verdict fine (or manual command) but no motion → command path.**
Issue a manual command and read both acks:

```bash
curl -s -X POST -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    http://<gpu-host>:8080/api/gates/<gate-id>/command -d '{"kind":"OPEN_GATE"}'
```

The POST response is the *receipt* ack; the *completion* ack arrives
on `/ws/events` (correlate by `commandId` — the double-ack contract).
A completion ack carrying an error names the fault:

- `Faulted(MotorTimeout)` — the gate didn't reach its limit switch in
  time: obstruction, motor, or limit-switch wiring.
- `LimitSwitchConflict` at boot — both or neither reed asserted; the
  firmware refuses to move from an unverified position. That refusal
  is the safety logic working; fix the switch/magnet.
- A blocked safety beam **rejects** `CLOSE_GATE` with a reasoned
  error, and trips a stop-and-reverse if it fires mid-close
  (UL 325-style policy).
- `unsupported on the residential controller profile` —
  `PULSE_RELAY`/`LATCH_*` are rejected by the current firmware
  profile by design.

**Clearing a fault:** there is **no `CLEAR_FAULT` wire command**
(known risk R5 in the
[bench plan](hardware/11-bench-validation-plan.md#known-risks-and-open-items)).
The documented workaround is `{"kind":"REBOOT"}`, which re-runs
boot-time position resolution — but it only helps if the underlying
cause is fixed first (a limit switch actually asserted, beam clear),
otherwise the gate boots straight back into `Faulted`.

**Hardware level.** With physical access: serial console
(`idf.py -p /dev/ttyACM0 monitor`), relay LEDs, reed/beam wiring. The
Stage 3 debug table in the
[bench plan](hardware/11-bench-validation-plan.md#stage-3--plaintext-grpc-end-to-end)
covers the classic signatures (relay polarity jumper, reed wiring,
brown-out resets on relay edges).

### 8.2 Server down

What the gates actually do — verified against the firmware
(`gate_rpc/control_client.hpp`, `gate_control/gate_controller.hpp`,
`firmware/main/main.cpp`):

- All safety logic is **on-device**: the state machine, motor
  watchdog, limit switches, and beam stop/reverse keep running with
  no server. A gate mid-travel finishes (or faults) normally.
- The gate then **holds its last commanded state**. Auto-close is
  compiled off (`auto_close_ms = 0` — the policy is deferred to
  remote config), so a gate that was open *stays open* until the
  server returns. Decide per site whether that is acceptable during
  an outage; if not, close gates before planned server work.
- The firmware reconnects forever with exponential backoff, 1 s
  doubling to 30 s, without rebooting; after the server returns,
  expect every gate back in `/api/status` within ~35 s (the soak-test
  bound). No field visit is ever needed for a server restart.
- What is lost meanwhile: ALPR entry (no verdicts), dashboard
  commands, telemetry.

Getting traffic through during the outage: the repo defines **no
remote manual-release path** — the LATCH/RELEASE command kinds are
rejected by the residential profile, and the server is down anyway.
Use the gate motor's own manual release (per
[ADR-008](decisions/ADR-008-gate-actuator-interface.md), motor-side
safety and mechanisms stay inside the Centurion-class controller —
its physical release key is the vendor-documented path) or hold the
gate open at the motor per your site's procedure.

Restoring the server: `systemctl status gate-server` → journal → if
it's a bad binary after an upgrade, `.prev` rollback (§7); if it's a
bad cert/secret, the journal names the unreadable file (§2).

### 8.3 Suspected compromised gate or certificate

Current posture (single-site, known risk R7 in the
[bench plan](hardware/11-bench-validation-plan.md#known-risks-and-open-items)):
all field clients share one `gate-client.pem` leaf, and there is no
CRL/revocation infrastructure. Consequences:

1. **Contain first at the network**: disable the compromised gate's
   switch port (or pull its cable). mTLS cannot distinguish it from
   its siblings while the leaf is shared.
2. **Re-key**: a compromised shared leaf means reissuing the
   `gate-client` leaf from the offline CA (§4.3), installing it in
   `firmware/main/certs/` and rebuilding/re-flashing every remaining
   gate, plus updating gate-vision's copy under `/etc/gate/tls` if it
   uses the same identity. The CA itself is uncompromised (its key
   was never on any gate), so server/dashboard leaves stand.
3. **Afterwards**, adopt the fleet note from
   [`docs/security.md`](security.md): one leaf per gate
   (SAN = gate id), so the next incident is a single-leaf revocation
   instead of a fleet re-key.

Also rotate the admin password and JWT secret (§4.1–4.2) if the
compromised device could have observed dashboard traffic.

### 8.4 Dashboard 401s everyone

Not necessarily an attack — three benign causes, in likelihood order:

1. **Token TTL expired.** Default 8 h (`expiresInMin: 480` in the
   login response). Everyone re-logs-in; nothing to fix.
2. **Backend restarted without a pinned secret.** Startup line says
   `secret=per-process` — every restart mints a new signing secret
   and orphans all tokens. Fix permanently by pinning
   `/etc/gate/jwt.secret` (§4.2).
3. **Deliberate secret rotation** (§4.2) — the intended side effect.

Only monitoring stays green during a 401 storm by design
(`/api/health`, `/api/status`, `/ws/events` are unauthenticated), so
the guard booth keeps its view while admins re-authenticate. If
re-login itself fails: 503 = auth not configured (see §3 caveat);
401 = wrong credential — re-mint the hash (§4.1).

### 8.5 OTA stuck or rejected

Every rejection arrives as a reasoned error ack (watch `/ws/events`)
and leaves the running slot untouched; the gate's LED shows the
deny-flash pattern on failure. Common cases:

| Symptom / ack | Cause | Fix |
|---|---|---|
| Same-version refusal | manifest `target_version` equals running `fwVersion` | that's the validator working; bump the version (§5) |
| Digest mismatch | served binary doesn't match the manifest (partial copy, wrong file, tamper) | re-copy **both** `gate_firmware.bin` and `manifest.json` to `/srv/gate/firmware` from the same release |
| Signature rejected | manifest signed with a key that doesn't match the gate's `CONFIG_GATE_OTA_PUBKEY` | confirm you signed with the site's `ota-signing.key`; a key rotation requires re-provisioning the pubkey via a firmware build first |
| `an OTA update is already running` | one update at a time per gate | wait; the OTA task is short-lived. If it never completes, `{"kind":"REBOOT"}` — a reboot mid-download boots the old slot cleanly and a fresh `BEGIN_OTA` succeeds (bench check 5.6) |
| Fetch fails / no progress at all | nginx site down, or the gate's manifest URL is wrong | `curl -sI http://<gpu-host>:8081/firmware/manifest.json` from the LAN; `sudo nginx -t && systemctl status nginx`. Remember the Kconfig default URL uses port **8080** — the drift flagged as risk R2; the built firmware must point at 8081 (or the 8444 TLS mirror) |
| Installed, then old version reports again | new image crash-looped; bootloader auto-reverted | that is the rollback guard working — capture the serial backtrace and escalate (§9) |

Also check the journal on the gate-server side for the command
round-trip, and confirm the manifest on disk:
`sha256sum /srv/gate/firmware/gate_firmware.bin` must equal
`image_sha256` in `manifest.json`.

---

## 9. Escalate to engineering

These symptoms are firmware/server bugs, not operations problems —
don't burn the night on them:

- A control stream that connects and then dies **repeatedly** (the
  teardown reason repeats in the `gate-rpc` serial log) — framing or
  tracker bug; reproducible in `tests/rpc_framing/`.
- A gate whose stream is down and **never** reconnects without a
  reboot — worse than any soak failure; the backoff loop should
  retry forever.
- Unexpected device resets or panics (boot banners you didn't cause;
  panic backtraces on serial).
- First-connect TLS failures right after a gate power-cycle that
  clear up minutes later — the cold-clock/SNTP race (risk R3, bench
  check 4.5); note timestamps precisely.
- An OTA image that passes signature/digest, installs, and is then
  rolled back by the bootloader.
- gate-server crash-looping on input (not on config/cert errors —
  those are yours, §2).
- Unexplained telemetry gaps >3 s while the stream stays up.

Capture **before** restarting anything, and attach all of it:

```bash
journalctl -u gate-server -u gate-dashboard -u gate-vision \
    --since "-2 hours" > journal.txt
curl -s http://<gpu-host>:8080/api/status > status.json
# Serial capture from the affected gate (physical access):
idf.py -p /dev/ttyACM0 monitor | tee serial.log
```

For OTA issues add `manifest.json` and the `sha256sum` of the served
binary; for reconnect issues note the exact restart/cable-pull times
so the logs line up.
