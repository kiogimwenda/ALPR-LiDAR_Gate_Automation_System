# Site Commissioning Checklist — Residential Gate

**Scope:** taking a bench-validated field controller and the GPU-host stack
from the lab to a live residential gate — sliding or dual-leaf swing, the
first deployment tier ([ADR-008](decisions/ADR-008-gate-actuator-interface.md),
[ADR-012](decisions/ADR-012-fusion-sensing-hardware.md) residential = discrete
sensor pair).
**Position:** starts where the
[bench validation plan](hardware/11-bench-validation-plan.md) ends — its
[Stage 7 exit criteria](hardware/11-bench-validation-plan.md#stage-7--exit-criteria)
are this document's entry criteria. Assembly detail lives in the
[Build Book](hardware/BUILD_BOOK.md) guides 01–08; this checklist orders and
gates the work, it does not repeat the wiring diagrams.
**Out of scope:** commercial/industrial sites (boom barriers, fusion-tier
sensing) — later tier per the deployment strategy.

Every section ends in rows that feed the [sign-off sheet](#9-sign-off-sheet).
A row is signed by the person who *observed* the result, with date. Where the
repo leaves a decision open, this document names it as a **decision the
commissioner records** — a blank decision box is a blocked section, not a
skippable one.

---

## 1. Entry criteria

Do not book the site visit until every row here is green.

| # | Criterion | Evidence |
|---|---|---|
| 1.1 | Bench plan [Stage 7](hardware/11-bench-validation-plan.md#stage-7--exit-criteria) E1–E8 all green | Signed bench log |
| 1.2 | Bench risk register R1–R7 dispositions on hand — R1/R2 (drifted Kconfig defaults), R3 (cold-clock TLS outcome), R4 (actuator profile), R5 (no remote fault-clear), R7 (per-gate leafs) all resurface below | [Known risks](hardware/11-bench-validation-plan.md#known-risks-and-open-items) table annotated |
| 1.3 | Host suite green on the machine you will build the site firmware on | `ctest --test-dir build/debug-cpu --output-on-failure` |
| 1.4 | R4 actuator decision **recorded** (see [§3.3](#33-the-r4-decision-point--record-before-wiring-the-motor)) — if the pulse profile was chosen, the firmware change is merged and bench-retested first | Decision box in §3.3 filled |

### What to bring

**Parts** — the full [prototype BOM](../hardware/bom/prototype-bom.md),
including the three items the bench deferred: Hikvision camera (#4, [guide
04](hardware/04-hikvision-camera.md)), Unitree L1 LiDAR (#3, [guide
03](hardware/03-unitree-l1-lidar.md)) if this site takes the LiDAR now, and
the gate-motor interface consumables (hookup wire to the Centurion controller,
per [guide 05](hardware/05-relay-module.md)). The bench-validated controller
travels in its enclosure, already soaked.

**Tools** — the [Build Book tools table](hardware/BUILD_BOOK.md#tools-required)
plus: laptop with the repo, ESP-IDF exported (`. ~/esp/esp-idf/export.sh`),
and a USB-C cable — the baseline site firmware is flashed over USB on-site
(certs are baked at build time, so the image cannot be prepared before the
site PKI exists — see [§6](#6-pki--firmware-provisioning)).

**Credentials and key material**

| Item | Where it lives | Comes to site? |
|---|---|---|
| OTA signing key (`ota-signing.key`) | Offline release machine only ([ADR-009](decisions/ADR-009-ota-strategy.md), [docs/security.md](security.md)) | **Never** — only `ota-pubkey.hex` and pre-signed manifests travel |
| Site CA key (`ca.key`) | The designated PKI machine (§6.1) | Only if the PKI machine is the site laptop; otherwise pre-issued leafs travel |
| Admin password | Minted on-site with the owner present (§8) | Minted at handover |
| Camera admin password | Set during [guide 04](hardware/04-hikvision-camera.md) activation | Set on-site |

---

## 2. Site survey

Complete before any drilling. One visit with a tape measure and a phone
camera is cheaper than a return trip with a masonry bit.

| # | Check | Criterion | ✔ |
|---|---|---|---|
| 2.1 | Mains at the gate pillar | 240V outlet or spur within reach of the enclosure position; breaker + RCD on the circuit; AC-side work booked with an electrician ([guide 07](hardware/07-power-supply.md) safety notes — 240V is lethal, not DIY) | ☐ |
| 2.2 | Conduit runs | Existing or plannable conduit for: mains → enclosure, enclosure → motor controller, beam TX/RX across the opening, reed switches → enclosure, Cat5e/Cat6 → camera and (if fitted) LiDAR | ☐ |
| 2.3 | Camera geometry | Mounting point available at **2–3 m height, angled down 15–30°**, plates readable at **3–10 m** approach distance with the 2.8 mm lens (longer lanes need the 4 mm variant) — [guide 04](hardware/04-hikvision-camera.md). Note night lighting and headlight-glare direction | ☐ |
| 2.4 | LiDAR mount (if fitted this visit) | Pillar or overhead position **2–3 m above ground looking down the approach lane**; IP54 only — needs an overhang or windowed housing outdoors ([guide 03](hardware/03-unitree-l1-lidar.md)) | ☐ |
| 2.5 | LAN reach | Ethernet run gate → server room ≤ 100 m; switch position inside the field enclosure (the TL-SG1005P is not waterproof — [guide 08](hardware/08-network-switch.md)) | ☐ |
| 2.6 | Gate motor | Identify the installed controller. Record make/model. Confirm dry-contact **TRIGGER terminal** access with the panel open ([ADR-008](decisions/ADR-008-gate-actuator-interface.md); Centurion D5-Evo for sliding, R5 for dual-leaf are the reference models). Confirm limit-switch mounting points at both travel ends and a beam line across the opening | ☐ |
| 2.7 | Gate type recorded | Sliding (one trigger) vs dual-leaf (two triggers) — drives the §3 wiring plan per the ADR-008 wiring tables | ☐ |
| 2.8 | Server room | Power, ventilation, and physical security for the GPU host ([guide 09](hardware/09-gpu-server.md)); Ethernet path to the gate switch | ☐ |
| 2.9 | Power-loss behaviour | Phase 1 decision #5 (see the README clarifications table): residential gates **fail-safe open** on power loss. This is a *motor-controller* behaviour (battery backup + controller config), not firmware. Record what the installed Centurion actually does and whether it matches | ☐ |

**Survey decision the commissioner records:** camera commissioned on this
visit, or deferred? The control path (§5–§7) is fully commissionable without
it; the detection acceptance test then runs on a scripted scenario (§7, A8)
until the camera lands.

---

## 3. Physical install

Follow the Build Book guides **in this order** — it matches the
[build order table](hardware/BUILD_BOOK.md#build-order) but starts from an
already-assembled, bench-soaked controller.

### 3.1 Enclosure, power, controller

| # | Step | Reference | ✔ |
|---|---|---|---|
| 3.1.1 | Mount the IP65 enclosure at the pillar; gland all conduit entries | [guide 07](hardware/07-power-supply.md) | ☐ |
| 3.1.2 | Electrician lands mains → 12V PSU (breaker + RCD verified) | [guide 07](hardware/07-power-supply.md) | ☐ |
| 3.1.3 | Verify rails before connecting loads: 12V reads 11.8–12.3 V, buck output 4.9–5.2 V; 3 A fuse on 12V, 2 A fuse on 5V; 1000 µF brown-out cap fitted across the 5V rail | [guide 07](hardware/07-power-supply.md), bench Stage 0 | ☐ |
| 3.1.4 | Mount the field controller + relay module + switch in the enclosure; W5500 wiring untouched since the bench soak (pin map pinned in [`firmware/sdkconfig.defaults`](../firmware/sdkconfig.defaults)) | [guide 02](hardware/02-w5500-ethernet.md) | ☐ |

### 3.2 Sensors

| # | Step | Reference | ✔ |
|---|---|---|---|
| 3.2.1 | Reed limit switches at both travel ends; magnets pass within actuation range; wired to GPIO 6 (open) / 7 (closed) | [guide 06](hardware/06-safety-sensors.md) | ☐ |
| 3.2.2 | Safety beam TX/RX across the opening; **RX indicator LED confirms alignment** (LED off = blocked/misaligned); wired to GPIO 15 | [guide 06](hardware/06-safety-sensors.md) | ☐ |
| 3.2.3 | Camera mounted per surveyed geometry (§2.3), PoE from switch port 1 — if commissioned this visit | [guide 04](hardware/04-hikvision-camera.md) | ☐ |
| 3.2.4 | LiDAR mounted per §2.4 — if fitted this visit | [guide 03](hardware/03-unitree-l1-lidar.md) | ☐ |

### 3.3 The R4 decision point — record before wiring the motor

⚠ **No wire lands on a motor terminal until this box is filled.** The bench
plan's Stage 7 handoff note flags the conflict: the shipped firmware's
residential profile drives **two motion contactors** and rejects
`PULSE_RELAY`/`LATCH_*` commands
([`firmware/main/main.cpp`](../firmware/main/main.cpp)), while
[ADR-008](decisions/ADR-008-gate-actuator-interface.md) specifies a single
dry-contact **trigger pulse** into the Centurion controller (pulse-to-toggle).

| Decision | Chosen (✔ one) | Recorded by / date |
|---|---|---|
| **(a) Pulse profile implemented** — firmware change merged, bench-retested (Stage 3 re-run), relay 1 → TRIGGER per the ADR-008 wiring table | ☐ | |
| **(b) Two-relay profile retained** — the Centurion is wired so that the two motion contacts map onto its inputs; the exact terminal mapping for the installed controller model is documented in the site file | ☐ | |

Then wire per the recorded decision: relay COM/NO contacts only, low-voltage
signal only — motor drive, soft-start, and anti-crush stay inside the
Centurion ([guide 05](hardware/05-relay-module.md) safety notes; never land
mains on the relay terminals).

| # | Step | ✔ |
|---|---|---|
| 3.3.1 | Motor interface wired per the recorded R4 decision; continuity check COM–NO shows relay actuation before the motor is trusted | ☐ |
| 3.3.2 | Manual gate operation via the Centurion's own controls still works (our interface is additive, not a replacement) | ☐ |

---

## 4. Network

The Phase 1 network constraint (README clarifications, #7): same LAN as the
GPU server, **dedicated VLAN**, sub-millisecond latency.

**Segmentation decision the commissioner records:** the reference TL-SG1005P
is *unmanaged* — it cannot tag VLANs. The constraint is satisfied either by
**(a)** a physically dedicated gate segment (the gate switch + the server NIC
carry nothing else — the reference topology), or **(b)** a managed
switch/router upstream carving a gate VLAN. Record which; if (b), record the
VLAN ID.

| # | Check | Criterion | ✔ |
|---|---|---|---|
| 4.1 | Addressing | [Build Book IP plan](hardware/BUILD_BOOK.md#network-ip-address-plan): camera `192.168.1.10`, ESP32 `192.168.1.20`, GPU host `192.168.1.50`, LiDAR `192.168.1.100`, subnet `192.168.1.0/24` | ☐ |
| 4.2 | Static leases | DHCP reservation for the controller MAC at `.20` (cert SANs and the IP plan assume it — bench Stage 2); camera set static at `.10` per [guide 04](hardware/04-hikvision-camera.md) | ☐ |
| 4.3 | PoE budget | TL-SG1005P: 65 W total across 4 PoE ports; the camera is the only PoE consumer in the reference build — headroom recorded if more cameras planned ([guide 08](hardware/08-network-switch.md)) | ☐ |
| 4.4 | Latency | All devices ping < 1 ms across the switch ([guide 08](hardware/08-network-switch.md) step 8) | ☐ |
| 4.5 | Time source | LAN NTP running on the GPU host (chrony) and reachable from the gate segment — R3 standard site infrastructure regardless of the cold-clock outcome (bench Stage 4) | ☐ |

### Port exposure

The stack listens on ([`deployment/README.md`](../deployment/README.md),
[`deployment/config/`](../deployment/config/),
[`deployment/nginx/gate-ota.conf`](../deployment/nginx/gate-ota.conf)):

| Port | Service | Who needs it | Off-segment exposure |
|---|---|---|---|
| 50051 | gate-server gRPC (mTLS) | Firmware, gate-vision, dashboard backend | **None** — gate segment only |
| 8080 | gate-dashboard HTTP + `/ws/events` | Operator browser | Only if the operator sits off-segment; admin routes are JWT-gated (§5), monitoring stays open by design |
| 8081 | nginx OTA, plain http | Nothing, once TLS firmware ships (it fetches 8444) | **None** — may be firewalled off entirely at a TLS site |
| 8444 | nginx OTA, https mirror | Firmware OTA fetch | **None** — gate segment only |

If the gate segment is routed to a wider estate network at all, the firewall
posture is: 8080 to the operator network, everything else segment-local.

---

## 5. Server provisioning

Per [`deployment/README.md`](../deployment/README.md), on the GPU host:

```bash
cmake --preset release && cmake --build --preset release
(cd dashboard/frontend && npm ci && npm run build)
sudo deployment/install-server.sh --build build/release --www dashboard/frontend/build
```

Then bring the configs to the **production posture** before first start.
[docs/security.md](security.md) is the contract: plaintext and an open admin
surface are *legal but loudly logged dev postures* — a commissioned site runs
mTLS everywhere and an authenticated admin surface, no exceptions.

| # | Step | ✔ |
|---|---|---|
| 5.1 | `/etc/gate/gate-server.env`: `SITE_ID` set for this site; **`TLS_EXTRA_ARGS` uncommented** (server pair + CA + `--require-client-cert`) | ☐ |
| 5.2 | `/etc/gate/gate-dashboard.env`: `SITE_ID` matches; **`TLS_EXTRA_ARGS` uncommented**; **`AUTH_EXTRA_ARGS` uncommented** with the hash from `scripts/gen-admin-hash.sh` (§8 — minted with the owner) and `--jwt-secret-file /etc/gate/jwt.secret` (single line of high-entropy text, mode 0640 root:gate, so sessions survive backend restarts) | ☐ |
| 5.3 | `/etc/gate/gate-vision.env`: `SITE_ID`/`GATE_ID` match; **`TLS_EXTRA_ARGS` uncommented** (gate-client identity) | ☐ |
| 5.4 | ALPR models installed under `/opt/gate/models` — built per [`scripts/export-models/README.md`](../scripts/export-models/README.md) (`convert_onnx_to_trt.py`, verify with `inspect_engine.py`). Required before the camera path goes live; skippable only if the camera is deferred (§2 decision) | ☐ |
| 5.5 | `gate-vision.env` `SOURCE_ARGS`: the production `--camera rtsp://…` form with the real camera credentials and the *site* camera IP (`192.168.1.10` per the IP plan — the env file's example IP is not the plan's), `--detector-engine`/`--recognizer-engine`/`--ocr-dictionary` pointing into `/opt/gate/models`. If the camera is deferred: the `--scenario` form for acceptance testing (§7 A8) | ☐ |
| 5.6 | `sudo systemctl restart gate-server gate-dashboard` after §6.1 installs the TLS material; then `sudo systemctl enable --now gate-vision` (the installer deliberately leaves gate-vision disabled until its source is configured) | ☐ |
| 5.7 | **Journal check:** `journalctl -u gate-server -u gate-dashboard -u gate-vision` shows **no plaintext warning and no open-admin warning** on any unit — the loud-degrade contract of [docs/security.md](security.md) read in reverse | ☐ |

---

## 6. PKI + firmware provisioning

### 6.1 Site PKI

**PKI-machine decision the commissioner records:** which machine mints and
keeps the site CA. `ca.key` is needed only to issue certs — it is not
required at runtime by any process and never leaves that machine
([docs/security.md](security.md)).

```bash
./scripts/gen-tls-certs.sh site-pki 192.168.1.50     # server IP as SAN — mandatory
sudo install -d -m 0755 /etc/gate/tls
sudo install -m 0640 -o root -g gate site-pki/{ca,server,dashboard}.pem \
     site-pki/{server,dashboard}.key /etc/gate/tls/
```

(Also install `gate-client.pem`/`gate-client.key` to `/etc/gate/tls/` for
gate-vision, per its env file.) The script refuses to overwrite an existing
CA — one CA per site, minted once.

**R7 disposition (record):** this is a single-gate residential site — the
shared `gate-client` leaf is the accepted posture, per the bench risk
register. First multi-gate site: issue **one leaf per gate (SAN = gate id)**
so a compromised controller can be revoked without re-keying
([`firmware/main/certs/README.md`](../firmware/main/certs/README.md),
[docs/security.md](security.md)); note that `gen-tls-certs.sh` does not
automate per-gate leafs today — that is tooling work the multi-gate site
inherits.

| # | Check | ✔ |
|---|---|---|
| 6.1.1 | PKI minted on the recorded machine; `ca.key` custody recorded | ☐ |
| 6.1.2 | `/etc/gate/tls` installed, root:gate, keys 0640 | ☐ |
| 6.1.3 | nginx 8444 TLS mirror **uncommented** in `/etc/nginx/sites-available/gate-ota` (the block ships commented until certs exist — header comments in [`gate-ota.conf`](../deployment/nginx/gate-ota.conf)); `sudo nginx -t && sudo systemctl reload nginx` | ☐ |
| 6.1.4 | R7 disposition box above filled | ☐ |

### 6.2 Firmware site build

Per [`firmware/main/certs/README.md`](../firmware/main/certs/README.md):

```bash
cp site-pki/ca.pem          firmware/main/certs/site_ca.pem
cp site-pki/gate-client.pem firmware/main/certs/gate.pem
cp site-pki/gate-client.key firmware/main/certs/gate.key
```

Then `idf.py menuconfig → Gate Firmware Configuration`. ⚠ **R1/R2: two
defaults are wrong for the reference IP plan and MUST be set explicitly** —
`GATE_SERVER_HOST` defaults to `192.168.1.10` (the *camera's* address) and
`GATE_OTA_MANIFEST_URL` defaults to port `8080` (the *dashboard's* port).
Every value below is confirmed by hand, none trusted from defaults:

| Kconfig | Site value | ✔ |
|---|---|---|
| `CONFIG_GATE_ID` | This gate's site-unique id (reference: `gate-01` — must match `GATE_ID` in `gate-vision.env`) | ☐ |
| `CONFIG_GATE_SERVER_HOST` | `192.168.1.50` (**R1** — override the drifted default) | ☐ |
| `CONFIG_GATE_SERVER_PORT` | `50051` | ☐ |
| `CONFIG_GATE_TLS_ENABLE` | `y` | ☐ |
| `CONFIG_GATE_SNTP_SERVER` | `192.168.1.50` (LAN chrony — `pool.ntp.org` does not resolve on an isolated segment; R3) | ☐ |
| `CONFIG_GATE_OTA_MANIFEST_URL` | `https://192.168.1.50:8444/firmware/manifest.json` (**R2** — TLS mirror, not 8080/8081) | ☐ |
| `CONFIG_GATE_OTA_PUBKEY` | 64-hex pubkey from `scripts/gen-ota-keys.sh` output (`ota-pubkey.hex`). ⚠ Empty legally degrades OTA to SHA-256-only with a warning — never acceptable at a commissioned site | ☐ |

| # | Step | ✔ |
|---|---|---|
| 6.2.1 | `idf.py build` succeeds — the fail-closed check: with TLS on, a missing PEM fails the build, so a successful build *is* the provisioning proof | ☐ |
| 6.2.2 | Flash over USB: `idf.py -p /dev/ttyACM0 flash monitor`; boot banner version recorded in the site file | ☐ |
| 6.2.3 | **Signed baseline published**: on the offline release machine, `scripts/sign-ota-manifest.sh --bin firmware/build/gate_firmware.bin --version <ver> --url https://192.168.1.50:8444/firmware/gate_firmware.bin --key ota-keys/ota-signing.key --key-id <site>-<year>`; copy `gate_firmware.bin` + `manifest.json` to `/srv/gate/firmware/` on the GPU host; `curl -skI https://192.168.1.50:8444/firmware/manifest.json` returns 200 + `Cache-Control: no-cache` | ☐ |
| 6.2.4 | R3 cold-clock disposition carried: if the bench recorded a failing epoch-clock handshake and the fix is not yet merged, record the operational mitigation here (chrony present; observed reconnect-after-sync behaviour noted) | ☐ |

---

## 7. Acceptance tests

The on-site mirror of the E2E suite (`e2e_local_stack`, `e2e_tls_stack`,
`e2e_auth_stack`, `e2e_vision_stack` — what each asserts is tabulated in
[docs/security.md](security.md)), now with a real gate on the end. Gate
travel happens: clear the opening of people and vehicles before starting.
Run in order; every row lands on the sign-off sheet.

Commands assume the dashboard SPA for normal driving; the raw API equivalents
(`/api/auth/login`, `/api/gates/{gate_id}/command`, `/api/status`,
`/ws/events`, `/api/allowlist`) are shown where a terminal is more precise.

| # | Test | Procedure | Expected | ✔ |
|---|---|---|---|---|
| A1 | Admin auth gate | Request a gate command with no token; then log in via the SPA (or `POST /api/auth/login`) and repeat | Bare request rejected (401); authenticated command accepted. Monitoring (`/api/status`) open without a token — by design, the guard-booth view survives session expiry | ☐ |
| A2 | Command round-trip, real travel | Authenticated OPEN from the dashboard | Immediate ack; gate physically opens; motion stops at the open limit; completion ack on `/ws/events` (the double-ack contract); state `Open` in the dashboard | ☐ |
| A3 | Close round-trip | Authenticated CLOSE | Mirror of A2 to the closed limit | ☐ |
| A4 | Safety beam interrupt | Command CLOSE; break the beam mid-travel with a test object (never a limb) | Motion stops on `SafetyBeamObstacle`, then **reverses to opening** (UL 325-style policy, bench 3.6) | ☐ |
| A5 | Blocked-beam close refused | With the beam blocked, command CLOSE | Rejected with a reasoned error ack; gate does not move (bench 3.7) | ☐ |
| A6 | Motor timeout | Disconnect the motor trigger lead (or hold the gate per the installer's judgment); command OPEN | After the 30 s motor timeout: `Faulted(MotorTimeout)`, completion ack carries the fault. Recover via `REBOOT` command (**R5**: no remote `clear_fault()` — REBOOT is the documented field workaround) | ☐ |
| A7 | Allowlist seed | Seed one real resident plate via the SPA or `POST /api/allowlist` | Entry visible in the allowlist view; write is JWT-gated | ☐ |
| A8 | Detection round-trip — scripted | With the camera deferred: `SOURCE_ARGS=--scenario …` in `gate-vision.env` replaying one unknown plate then the seeded plate (the `events`/`offset_ms`/`plates` JSON shape used by [`tests/e2e/e2e_local_stack.py`](../tests/e2e/e2e_local_stack.py)); restart gate-vision | Unknown plate: **gate does not move**. Seeded plate: OPEN_GATE auto-dispatched, gate opens, audit actor `auto:<decision-id>` in the event stream | ☐ |
| A9 | Detection round-trip — live camera | Camera commissioned: drive a vehicle with the seeded plate up the lane at normal approach speed; repeat after dark (IR) | Same outcome as A8 from real optics; recognition at the surveyed distance band. Skipped-and-scheduled if the camera is deferred — record the return-visit date | ☐ |
| A10 | OTA no-op | Re-issue BEGIN_OTA against the already-installed baseline manifest | Refused with a reasoned ack — same-version offer rejected by `validate()` (bench 5.4). Confirms the full fetch-and-validate path against the 8444 mirror without flashing anything | ☐ |
| A11 | Reboot recovery | `REBOOT` command from the dashboard | Controller reboots, resolves position from the limit switches, mTLS stream re-establishes, telemetry resumes; no operator action needed | ☐ |
| A12 | Power-cycle recovery | Kill enclosure power ≥ 30 s, restore | Same recovery as A11 from cold. **Additionally record** the gate's physical behaviour during the outage vs the §2.9 fail-safe-open decision | ☐ |
| A13 | Journal posture re-check | `journalctl` sweep across all three units after the test session | Zero plaintext/open-admin warnings; no unexplained stream teardowns during the session | ☐ |

---

## 8. Handover

| # | Item | Detail | ✔ |
|---|---|---|---|
| 8.1 | Admin credential | Password minted **with the owner present** via `scripts/gen-admin-hash.sh` (prompted, never echoed, never in argv). The owner keeps the password; the installer retains only the hash in `/etc/gate/gate-dashboard.env`. Recovery path = installer re-mints the hash on request | ☐ |
| 8.2 | Key custody recorded | Site CA key: PKI machine per §6.1 decision (installer custody). OTA signing key: offline release machine (installer/vendor custody) — the site never holds it. JWT secret + TLS keys: on the GPU host, root:gate 0640 | ☐ |
| 8.3 | Camera credential | Camera admin password recorded in the site file; note it is embedded in `SOURCE_ARGS` in `/etc/gate/gate-vision.env` (mode-protected, flagged as a residual item in [docs/security.md](security.md)) | ☐ |
| 8.4 | Resident allowlist seeded | Full resident plate list loaded (A7 proved the path); owner shown the SPA allowlist screen: add, remove, and that these routes require login | ☐ |
| 8.5 | Operator orientation | Owner/guard walked through: dashboard monitoring view (open without login — survives session expiry), issuing open/close, what a `Faulted` state looks like and the REBOOT recovery (R5), and who to call. The operator's standing reference is the [operations runbook](runbook.md) — hand it over with the credentials | ☐ |
| 8.6 | Warranty and support notes | Motor safety functions (anti-crush, soft-start) remain inside the Centurion — the dry-contact interface does not void the motor warranty (ADR-008 rationale). Relay modules are a wear item (~100k mechanical cycles class, bench Stage 6 note). Record the support contact and the OTA release channel: updates arrive only as signed manifests from the installer | ☐ |
| 8.7 | Site file archived | Survey values (§2), R4/segmentation/PKI decisions, baseline firmware version + manifest key-id, soak + acceptance logs, this signed sheet — filed with the installer and a copy to the owner | ☐ |

---

## 9. Sign-off sheet

Commissioning is complete when every row is initialled. An `N/A` requires a
reason in the criterion column margin (e.g. "camera deferred — return visit
booked <date>").

| # | Item | Criterion | Pass | Initials | Date |
|---|---|---|---|---|---|
| S1 | Entry criteria | Bench Stage 7 green; R1–R7 dispositions in hand; R4 recorded before travel | ☐ | | |
| S2 | Site survey | §2 all rows; gate type + power-loss behaviour recorded | ☐ | | |
| S3 | Power | Rails in tolerance, fused, RCD on mains, brown-out cap fitted | ☐ | | |
| S4 | Sensors | Limits actuate at both travel ends; beam aligned (RX LED) | ☐ | | |
| S5 | Motor interface | Wired per recorded R4 decision; manual Centurion operation intact | ☐ | | |
| S6 | Network | IP plan + static leases; segmentation decision recorded; PoE budget noted; <1 ms pings; LAN NTP up | ☐ | | |
| S7 | Server posture | mTLS + admin auth live; no plaintext / open-admin warnings in any journal | ☐ | | |
| S8 | PKI | Site CA minted once, custody recorded; `/etc/gate/tls` installed; 8444 mirror serving; R7 disposition recorded | ☐ | | |
| S9 | Firmware | All seven Kconfig site values confirmed (R1/R2 overridden); TLS build passed fail-closed; baseline flashed; signed manifest published | ☐ | | |
| S10 | A1–A7 | Auth, command round-trips, beam interrupt + refusal, motor timeout, allowlist | ☐ | | |
| S11 | A8/A9 | Detection round-trip (scripted; live if camera commissioned — else return visit booked) | ☐ | | |
| S12 | A10–A13 | OTA no-op, reboot + power-cycle recovery, journal sweep | ☐ | | |
| S13 | Handover | Credentials custody, allowlist seeded, operator oriented, site file archived | ☐ | | |

**Commissioner:** ______________________ **Owner/representative:** ______________________ **Date:** ____________
