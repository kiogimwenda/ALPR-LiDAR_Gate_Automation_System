# Bench Bring-Up and Validation Plan — Field Controller

**Scope:** ESP32-S3 field controller ↔ GPU-host control plane, on real hardware.
**Position:** starts after the [Build Book](BUILD_BOOK.md) assembly guides (01–08) are complete; ends at the exit criteria in [Stage 7](#stage-7--exit-criteria), which gate the move to gate-motor integration ([ADR-008](../decisions/ADR-008-gate-actuator-interface.md)).
**Out of scope:** camera/LiDAR/inference bring-up ([04](04-hikvision-camera.md), [03](03-unitree-l1-lidar.md), [09](09-gpu-server.md)) — the GPU perception path has its own validation track and none of the stages below depend on it.

## Why this plan is shaped the way it is

Everything *pure* in the firmware is already proven on the host: the state
machine, the gRPC framing codec, the command tracker, and the OTA manifest
accept/reject rules all compile as host mirrors and run under Catch2 on every
commit (see [`firmware/README.md`](../../firmware/README.md) layout notes).
The bench therefore exists to validate exactly what a host test *cannot*
reach: real GPIOs and debounce, the W5500 over real SPI, nghttp2 over lwIP on
real silicon, esp-tls handshakes (explicitly deferred to bench in
[`docs/security.md`](../security.md) "known residual items"), flash A/B swaps,
and hours of wall-clock time.

That split is the debugging tool. Before every stage, run the host suite that
mirrors its logic — if the host test is green and the bench fails, the fault
is in hardware, wiring, or the integration layer, **not** in the logic the
test covers. Never debug transition logic with a multimeter.

### Bench stage → host-test mirror

| Bench stage | Host proof (run first, must be green) | A bench failure then isolates to |
|---|---|---|
| 1 — Flash + serial | [`tests/state_machine/`](../../tests/state_machine/) (`test_state_machine`), [`tests/firmware_integration/`](../../tests/firmware_integration/) | Power, USB, flash tooling, GPIO wiring — not transition logic |
| 3 — Plaintext gRPC | [`tests/rpc_framing/`](../../tests/rpc_framing/) (framing + command tracker), [`tests/rpc/`](../../tests/rpc/), [`tests/integration/`](../../tests/integration/), ctest `e2e_local_stack` | W5500/lwIP/nghttp2 on-silicon, LAN, server deployment — not the wire contract |
| 4 — TLS/mTLS | ctest `e2e_tls_stack`, `e2e_hardened_stack` | esp-tls, cert provisioning, device clock — not the PKI or exclusion rules |
| 5 — OTA | [`tests/ota_manifest/`](../../tests/ota_manifest/) (`test_ota_manifest`) | nginx, transport, flash writes/readback — not the accept/reject rules |
| 6 — Soak | `e2e_local_stack` (short-run analogue) | Thermals, power integrity, reconnect behaviour over real time |

Run the whole mirror in one shot from the repo root:

```bash
cmake --preset debug-cpu && cmake --build --preset debug-cpu
ctest --test-dir build/debug-cpu --output-on-failure
```

The `debug-cpu` preset builds `gate-server`, `gate-dashboard`, and `gate-sim`
without CUDA — the same CPU build the E2E suite uses, and the build this plan
deploys on the bench host.

## Conventions

- **Bench LAN** follows the [Build Book IP plan](BUILD_BOOK.md#network-ip-address-plan): GPU host `192.168.1.50`, ESP32-S3 `192.168.1.20`, subnet `192.168.1.0/24`.
- ⚠ **Two Kconfig defaults do not match that plan** and must be set explicitly in `idf.py menuconfig → Gate Firmware Configuration` before Stage 3: `CONFIG_GATE_SERVER_HOST` defaults to `192.168.1.10` (the *camera's* address in the IP plan) and `CONFIG_GATE_OTA_MANIFEST_URL` defaults to port `8080` (the *dashboard's* port — the nginx OTA site listens on `8081`, per [`deployment/nginx/gate-ota.conf`](../../deployment/nginx/gate-ota.conf)). Both are logged in [Known risks](#known-risks-and-open-items).
- **ESP-IDF environment** is assumed exported in every shell (`. ~/esp/esp-idf/export.sh`); build/flash/monitor workflow per [`firmware/README.md`](../../firmware/README.md). On WSL2, attach the board with `usbipd-win` first ([guide 01](01-esp32-s3-devkit.md)).
- Each stage lists **Prerequisites → Procedure → Pass criteria → Debug/rollback**. Do not start a stage until the previous one's pass criteria are all met — later stages assume them silently.

---

## Stage 0 — Bench setup

### Day-one BOM subset

From [`hardware/bom/prototype-bom.md`](../../hardware/bom/prototype-bom.md) —
the camera (#4), LiDAR (#3), and gate motor (#20) are **not** needed for any
stage in this plan; defer that spend until this plan exits.

| BOM # | Item | Needed from stage |
|---|---|---|
| 1 | ESP32-S3-DevKitC-1 (N16R8) | 1 |
| 2 | W5500 SPI Ethernet module | 2 |
| 15, 13, 14, 16 | USB-C data cable, breadboard, jumper wires, hookup wire | 1 |
| 8, 12 | TP-Link TL-SG1005P switch, Cat5e cables | 2 |
| 17–19 | GPU host (any x86 box works for this plan — the CPU build needs no GPU) | 3 |
| 5 | 4-channel 5V relay module | 3 |
| 6, 7 | Safety beam pair, 2× NC reed switches | 3 |
| 9, 10 | 12V/2A PSU, LM2596 5V buck | 6 (USB powers stages 1–5) |
| 11 | IP65 enclosure | 6 (thermal observation) |

### Lab tools

Multimeter, small flathead screwdriver, wire strippers, soldering iron (if
headers arrive loose) — the [Build Book tools table](BUILD_BOOK.md#tools-required).
A current-limited bench PSU is optional but recommended for first power-on of
the 12V rail; otherwise the BOM's enclosed PSU is the bench supply. A USB
power meter (or the multimeter in series) covers the current-draw checks.

### Safety notes — read before powering anything

- **240V mains is lethal.** The 12V PSU's AC side is electrician work; use a breaker + RCD on the bench circuit ([guide 07](07-power-supply.md) safety notes).
- **Nothing in this plan switches motor current.** [ADR-008](../decisions/ADR-008-gate-actuator-interface.md) keeps motor drive, soft-start, and inrush inside the Centurion controller; our relays only ever switch low-voltage signal contacts. Never land mains on the relay screw terminals ([guide 05](05-relay-module.md)).
- **Brown-out guard:** relay coil switching can dip the 5V rail and reset the ESP32 — keep the 1000 µF electrolytic across the 5V rail near the ESP32 from [guide 07](07-power-supply.md) fitted before Stage 3.
- Fuse the DC rails (3 A on 12V, 2 A on 5V) before the soak stage.

**Pass criteria:** all day-one parts on the bench; 12V and 5V rails measure
11.8–12.3V / 4.9–5.2V unloaded; host suite green (`ctest` above).

---

## Stage 1 — Flash + serial smoke

Proves: toolchain → flash → boot → state-machine startup on real silicon.

**Prerequisites:** Stage 0; board on USB; `test_state_machine` and
`test_firmware_integration` green.

**Procedure**

```bash
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor        # or /dev/ttyUSB0
```

**Pass criteria**

| # | Check | Expected |
|---|---|---|
| 1.1 | Boot banner | `gate-firmware <version> starting (idf v…)` — version matches `firmware/components/gate_drivers/src/version.cpp` |
| 1.2 | Chip line | `chip: esp32s3 rev …, 2 cores, WiFi BLE embedded-flash` |
| 1.3 | Bare-bench boot state | `Faulted` with `LimitSwitchConflict` + `safety beam blocked at boot` warning — **by design**: unwired limit inputs read "mid-travel" and the unwired beam input reads "blocked" (failsafe). Exact log lines in [`firmware/README.md`](../../firmware/README.md). |
| 1.4 | Heartbeat log | `gate-ctrl: heartbeat: state=Faulted … beam=blocked limits[open=0 closed=0]` every 5 s (`heartbeat_period_ms` in `main.cpp`) |
| 1.5 | RPC behaviour with no network | `gate-rpc: started (server=…)` then quiet — the client blocks until got-ip, no crash, no watchdog reset |
| 1.6 | Current draw | Record USB 5V draw at idle. No repo baseline exists yet — **this measurement becomes the baseline** (rule of thumb: an S3 devkit + logging sits in the 100–200 mA band; a value near 0 or above 500 mA means a short or a boot loop). |

**Debug / rollback**

| Symptom | Likely cause | Action |
|---|---|---|
| No serial device | WSL2 USB not attached | `usbipd` attach per [guide 01](01-esp32-s3-devkit.md) |
| Boot loops before banner | Power/flash corruption | `idf.py erase-flash` then re-flash; try a different cable/port |
| Banner but panic later | Real firmware bug on silicon | Capture the backtrace; reproduce the scenario in `tests/firmware_integration/` before touching hardware again |

Rollback is trivial at this stage: `idf.py erase-flash` returns the board to
factory state.

---

## Stage 2 — W5500 bring-up

Proves: SPI wiring, the `ethernet_init` driver config, DHCP, and L3
reachability.

**Prerequisites:** Stage 1; W5500 wired per [guide 02](02-w5500-ethernet.md);
switch powered; a DHCP server on the bench LAN (or the router in the IP plan).

**Wiring check (before power):** the pin map is pinned in
[`firmware/sdkconfig.defaults`](../../firmware/sdkconfig.defaults) — verify
each jumper against it, not against memory:

| Signal | GPIO (`CONFIG_ETHERNET_SPI_*`) |
|---|---|
| MISO / MOSI / SCLK / CS | 13 / 11 / 12 / 10 |
| INT / RST | 9 / 8 |
| SPI clock | 20 MHz (`CONFIG_ETHERNET_SPI_CLOCK_MHZ`) |

**Procedure:** power on with the Ethernet cable in a switch port, watch the
monitor, then from the GPU host: `ping <device-ip>`. Reserve a static DHCP
lease for the device MAC at `192.168.1.20` while you're in the router — the
Build Book IP plan assumes it and Stage 4's cert SANs stay simpler.

**Pass criteria**

| # | Check | Expected |
|---|---|---|
| 2.1 | RJ45 LEDs | Green (link) within ~2 s of cable insertion; amber flickers on traffic |
| 2.2 | Log | `gate-fw: link up` then `network ready at 192.168.1.x` (DHCP complete) |
| 2.3 | Ping from host | <5 ms on-LAN, 0% loss over 60 s |
| 2.4 | Cable pull | `link down` logged within a few seconds; re-insert → link up + same or new lease, no reboot |

**Debug:** the failure-signature table in [guide 02](02-w5500-ethernet.md)
covers the classics — LEDs dark = power/cable; link but no IP = MISO/MOSI
swapped or CS/SCLK misplaced. If SPI errors persist on correct wiring, halve
`CONFIG_ETHERNET_SPI_CLOCK_MHZ` via menuconfig (long jumpers degrade at
20 MHz) and note it for the PCB revision.

---

## Stage 3 — Plaintext gRPC end-to-end

Proves: the ADR-011 stack (nanopb + framing + nghttp2 h2c) against the real
server, command round-trips into real relays, and the safety inputs — the
dev-posture topology from [`deployment/README.md`](../../deployment/README.md).

**Prerequisites:** Stage 2; relay module, both reed switches, and the beam
pair wired to the default `GateController::Pins` map
([`gate_controller.hpp`](../../firmware/components/gate_control/include/gate_control/gate_controller.hpp)):
relays on GPIO 4 (open) / 5 (close), limits on 6 (open) / 7 (closed), beam on
15, status LEDs 16/17/18. Wiring details per guides [05](05-relay-module.md)
and [06](06-safety-sensors.md). `e2e_local_stack` green on the host.

**Procedure — host side.** Deploy the CPU build with the real installer and
units, because the deployment surface is itself under test:

```bash
sudo deployment/install-server.sh --build build/debug-cpu \
    --www dashboard/frontend/build          # SPA optional; API works without it
sudo systemctl enable --now gate-server gate-dashboard
journalctl -u gate-server -f                # expect the loud "plaintext" dev warning
```

Runtime knobs live in `/etc/gate/gate-server.env` and
`/etc/gate/gate-dashboard.env` (defaults from
[`deployment/config/`](../../deployment/config/) are correct for this stage).
Then prove the server with the known-good client before involving firmware:

```bash
./build/debug-cpu/simulation/gate-sim --server 192.168.1.50:50051 --gate-id sim-01
curl -s http://192.168.1.50:8080/api/status   # sim-01 visible → server side is good
```

**Procedure — firmware side.** `idf.py menuconfig → Gate Firmware
Configuration`: set `CONFIG_GATE_SERVER_HOST=192.168.1.50` (overriding the
drifted default — see Conventions), confirm port `50051` and
`CONFIG_GATE_ID=gate-01`. Build, flash, park a magnet on the *closed* reed so
the gate boots into a known position, clear the beam path.

Command round-trip (dashboard UI, or raw — the admin surface is open in the
dev posture):

```bash
curl -s -X POST http://192.168.1.50:8080/api/gates/gate-01/command \
     -H 'Content-Type: application/json' -d '{"kind":"OPEN_GATE"}'
```

Simulate gate travel by hand: after commanding open, move the magnet from the
closed reed to the open reed within 30 s.

**Pass criteria**

| # | Check | Expected |
|---|---|---|
| 3.1 | Boot position | `state -> Closed`, heartbeat `limits[open=0 closed=1]` (not `Faulted`) |
| 3.2 | Stream up | `gate-rpc: Control stream opening to 192.168.1.50:50051`; `gate-01` in `/api/status` with fw version; telemetry at ~1 Hz (`telemetry_period_ms`) |
| 3.3 | OPEN round-trip | Immediate ack (`completed=false`) in the POST response; relay 1 (GPIO4) audibly clicks on; on reaching the open reed the relay drops and the completion ack arrives on `/ws/events` — the double-ack contract from `control_client.hpp` |
| 3.4 | CLOSE round-trip | Same, mirrored on relay 2 (GPIO5) and the closed reed |
| 3.5 | Motor timeout | Command open, *don't* move the magnet → after 30 s (`motor_timeout_ms`) relay drops, `Faulted(MotorTimeout)`, completion ack carries the fault |
| 3.6 | Beam interrupt | Block the beam mid-close → motor stops (`SafetyBeamObstacle`), then reverses to Opening (`reverse_on_beam=true`, UL 325-style — policy documented in `gate_controller.hpp`) |
| 3.7 | Blocked-beam close | With the beam blocked, `CLOSE_GATE` is rejected with a reasoned error ack |
| 3.8 | Reconnect | `systemctl restart gate-server` → firmware backs off (1 s doubling to 30 s) and reattaches without reboot |
| 3.9 | Relay idle state | At boot and after every sequence, both relay LEDs off (constructor drives relays off before anything else) |

**Debug / rollback**

- 3.1 fails both-limits or neither: reed polarity/wiring — see [guide 06](06-safety-sensors.md). The firmware refuses to move from an unverified position; that refusal *is* the logic working.
- Relay never clicks but the ack completes: trigger-polarity jumper on the module ([guide 05](05-relay-module.md) failure table).
- Firmware faulted and you need it back: there is **no wire command for `clear_fault()`** — issue `{"kind":"REBOOT"}` to re-run boot position resolution (flagged in [Known risks](#known-risks-and-open-items)).
- ESP32 resets when a relay fires: brown-out — fit the Stage 0 capacitor.
- Stream connects then dies repeatedly: capture the `gate-rpc` teardown reason; framing/tracker bugs reproduce in `tests/rpc_framing/` — do that before instrumenting hardware.

---

## Stage 4 — TLS/mTLS on hardware

Proves: the one security surface the host suite cannot — esp-tls handshake +
mTLS client identity on silicon ([`docs/security.md`](../security.md) residual
item). The exclusion properties (wrong-cert peers never reach the stack) are
already enforced by `e2e_tls_stack`; the bench re-checks them only where the
firmware is the peer.

**Prerequisites:** Stage 3; `e2e_tls_stack` and `e2e_hardened_stack` green.

**Procedure — host PKI first, proven with gate-sim before firmware:**

```bash
./scripts/gen-tls-certs.sh site-pki 192.168.1.50      # server IP as SAN — mandatory
sudo install -d -m 0755 /etc/gate/tls
sudo install -m 0640 -o root -g gate site-pki/{ca,server,dashboard}.pem \
     site-pki/{server,dashboard}.key /etc/gate/tls/
# /etc/gate/gate-server.env and gate-dashboard.env: uncomment TLS_EXTRA_ARGS
sudo systemctl restart gate-server gate-dashboard
./build/debug-cpu/simulation/gate-sim --server 192.168.1.50:50051 --gate-id sim-01 \
     --tls-ca site-pki/ca.pem --tls-cert site-pki/gate-client.pem --tls-key site-pki/gate-client.key
```

The server env's `--require-client-cert` makes this full mTLS; the journal
must **not** show the plaintext warning any more.

**Procedure — firmware.** Follow
[`firmware/main/certs/README.md`](../../firmware/main/certs/README.md)
verbatim: copy `ca.pem` → `site_ca.pem`, `gate-client.pem` → `gate.pem`,
`gate-client.key` → `gate.key` into `firmware/main/certs/`. In menuconfig set
`CONFIG_GATE_TLS_ENABLE=y`, point `CONFIG_GATE_SNTP_SERVER` at a reachable
time source (on an isolated bench LAN, run chrony on the GPU host and use
`192.168.1.50` — `pool.ntp.org` won't resolve), and move
`CONFIG_GATE_OTA_MANIFEST_URL` to `https://192.168.1.50:8444/firmware/manifest.json`
(the Kconfig help mandates the TLS mirror once TLS is on). Build, flash.

**Pass criteria**

| # | Check | Expected |
|---|---|---|
| 4.1 | Fail-closed build | With TLS enabled and any PEM missing, `idf.py build` **fails** (`EMBED_TXTFILES`) — verify once deliberately before provisioning; a gate never ships half-provisioned |
| 4.2 | mTLS stream | Control stream up, `gate-01` in the dashboard — now encrypted end to end |
| 4.3 | Plaintext intruder | The Stage 3 (non-TLS) firmware build against this server: refused at connect, gate never appears — the on-hardware mirror of `e2e_tls_stack`'s exclusion check |
| 4.4 | Wrong CA | Build once against a throwaway PKI (`gen-tls-certs.sh /tmp/rogue-pki`): handshake fails, client logs and backs off, no crash, no connection |
| 4.5 | Cold-clock handshake | Power-cycle the device and observe whether the *first* TLS connect (racing SNTP's first sync, clock ≈ epoch) succeeds or is rejected on certificate validity. **Record the outcome either way** — see the risk item below |

**Known risk — time vs. certificate validity.** The firmware initialises SNTP
(`main.cpp`) and deliberately omits telemetry timestamps until the first sync,
but nothing gates the TLS connect on time sync, and the leaf certs carry real
notBefore/notAfter windows (3 yr, per `gen-tls-certs.sh`). Whether the
epoch-clock handshake passes depends on mbedTLS time-checking configuration —
the repo does not pin this down. Proposed resolution: make 4.5's measured
outcome authoritative; if the cold handshake fails, gate the first connect on
SNTP sync (or persist last-known time in NVS) as a firmware fix, and keep LAN
NTP on the GPU host as standard site infrastructure either way.

**Rollback:** set `CONFIG_GATE_TLS_ENABLE=n` and re-comment the two
`TLS_EXTRA_ARGS` lines to return the whole bench to the Stage 3 posture. The
PEMs in `firmware/main/certs/` are gitignored — leave them in place.

---

## Stage 5 — OTA end-to-end

Proves: the ADR-009 pipeline (fetch → validate → stream → flash readback →
verify → A/B flip → rollback-guarded boot) over real nginx and real flash.
Every accept/reject rule is host-proven in
[`tests/ota_manifest/`](../../tests/ota_manifest/); the bench exercises the
transport and flash halves, plus the two negatives that must *never* regress.

**Prerequisites:** Stage 3 (plaintext) or Stage 4 (TLS — then use the 8444
mirror throughout). `test_ota_manifest` green.

**Procedure — one-time setup**

```bash
# nginx OTA site, per the header comments in deployment/nginx/gate-ota.conf:
sudo cp deployment/nginx/gate-ota.conf /etc/nginx/sites-available/gate-ota
sudo ln -s /etc/nginx/sites-available/gate-ota /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
# /srv/gate/firmware already exists — install-server.sh created it in Stage 3.

# Signing keys — on the OFFLINE release machine, never the bench host (ADR-009):
scripts/gen-ota-keys.sh ota-keys
```

Paste `ota-keys/ota-pubkey.hex` into menuconfig → `CONFIG_GATE_OTA_PUBKEY`,
rebuild, flash. This baseline **must** run with the pubkey set: an empty
`GATE_OTA_PUBKEY` legally degrades to SHA-256-only with a loud warning
(Kconfig help) — not an acceptable posture for validation.

**Procedure — happy path.** Bump `kVersion` in
`firmware/components/gate_drivers/src/version.cpp` (e.g. → `0.2.0`), then per
[`deployment/README.md`](../../deployment/README.md):

```bash
idf.py build
scripts/sign-ota-manifest.sh --bin firmware/build/gate_firmware.bin \
    --version 0.2.0 --url http://192.168.1.50:8081/firmware/gate_firmware.bin \
    --key ota-keys/ota-signing.key --key-id bench-2026
sudo cp firmware/build/gate_firmware.bin manifest.json /srv/gate/firmware/
curl -sI http://192.168.1.50:8081/firmware/manifest.json   # 200 + Cache-Control: no-cache
curl -s -X POST http://192.168.1.50:8080/api/gates/gate-01/command \
     -H 'Content-Type: application/json' -d '{"kind":"BEGIN_OTA"}'
```

**Pass criteria**

| # | Check | Expected |
|---|---|---|
| 5.1 | Happy path | OTA-pulse LED during download; success ack; reboot; `first boot of OTA image on ota_1 — marking valid` in the monitor; banner + telemetry report `0.2.0` |
| 5.2 | Tampered image | Flip one byte of the *served* binary after signing (`printf '\xff' \| sudo dd of=/srv/gate/firmware/gate_firmware.bin bs=1 seek=1000 conv=notrunc`), re-issue BEGIN_OTA → **rejected** on digest mismatch: error ack, deny-flash LED, no reboot, running version unchanged |
| 5.3 | Bad signature | Restore the binary, corrupt two hex chars of `ed25519_signature` in the manifest → **rejected**, same observable outcome. (Sign with a *different* key for the stronger variant — wrong-key, well-formed signature.) |
| 5.4 | Same-version offer | Re-serve the manifest matching the running version → refused by `validate()` with a reasoned ack (rule host-proven in `test_ota_manifest`) |
| 5.5 | Concurrency guard | Second BEGIN_OTA while one runs → `an OTA update is already running` error ack |
| 5.6 | Power-cut resilience | Pull power mid-download once → device boots the old slot cleanly; a fresh BEGIN_OTA succeeds |

5.2 and 5.3 are the whole point of the signing design: transport integrity is
*not* assumed (`gate-ota.conf` serves plain http on 8081 for exactly this
reason) — the digest + signature carry it. Both rejections must be observed on
hardware once, then they are regression-guarded by the host suite forever.

**Debug / rollback:** a failed or rejected OTA leaves the running slot
untouched by construction (A/B, `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` in
`sdkconfig.defaults`); worst case `idf.py flash` restores any state. If an
update installs but the new image crash-loops, the bootloader reverts to the
previous slot on its own — observing that once (optional drill: sign a build
with an early `abort()`) is worth the hour if time allows.

---

## Stage 6 — Sensor/actuation soak

Proves: the things only wall-clock time reveals — thermal drift, power
integrity under repeated relay switching, reconnect robustness, telemetry
continuity. Move the electronics into the enclosure and onto the 12V/5V PSU
(guide [07](07-power-supply.md)) for this stage; USB power hides exactly the
problems we're hunting.

**Prerequisites:** Stages 3–5 passed; DC rails fused; brown-out cap fitted.

**Procedure.** Run ≥8 h. Drive a cycle loop from the host (each iteration:
open, hand-move the magnet, close, restore — or park the magnets off both
reeds and accept a MotorTimeout fault + REBOOT per cycle if unattended, which
soaks the relay and fault paths instead of the happy path; either is valid,
**log which**):

```bash
i=0; while true; do
  curl -s -X POST http://192.168.1.50:8080/api/gates/gate-01/command \
       -H 'Content-Type: application/json' -d '{"kind":"OPEN_GATE"}'
  sleep 30
  curl -s -X POST http://192.168.1.50:8080/api/gates/gate-01/command \
       -H 'Content-Type: application/json' -d '{"kind":"CLOSE_GATE"}'
  sleep 30; i=$((i+1)); echo "cycle $i $(date -Is)" >> soak-cycles.log
done
```

Mid-soak, inject faults deliberately: pull the Ethernet cable ×5 (varying
durations 5 s–5 min), `systemctl restart gate-server` ×3, block the beam
during a close ×3.

**Metrics to record** (these become the field-deployment baseline — the repo
has none yet):

| Metric | How | Pass |
|---|---|---|
| Telemetry continuity | `/ws/events` capture or `journalctl -u gate-server`; count gaps >3 s outside injected faults | 0 unexplained gaps |
| Reconnect time after cable pull | link-down log → stream-up log | ≤ ~35 s (link detect + 30 s max backoff), every time, no reboot |
| Reconnect after server restart | restart → stream-up | Same bound; command round-trip works immediately after |
| Relay cycles | `soak-cycles.log` count | ≥ 400 cycles, zero missed actuations (generic Songle SRD-05VDC modules are rated ~100k mechanical — the soak is a lot-quality screen, not a life test) |
| Unexpected resets | Boot banners in the serial log | 0 (any reset = capture reason, stop the clock, fix, restart soak) |
| 5V rail during relay switching | Multimeter min-hold across the rail | ≥ 4.75 V |
| Temperatures at t0 / t+1h / t+8h | Spot-check buck converter, W5500, relay board, enclosure ambient | Stable by t+1h; buck < 60 °C in the closed enclosure |
| Heartbeat cadence | 5 s `gate-ctrl` heartbeats in serial log | Steady across the whole run |

**Debug:** resets correlated with relay edges → power integrity (bigger cap,
shorter leads, check the fuse holder); creeping buck temperature → derate or
relocate before the enclosure seals for field install; a single hung stream
that never reconnects is a firmware bug worth more than the rest of the soak —
capture the full serial + journal context.

---

## Stage 7 — Exit criteria

All rows green declares the **field controller hardware-validated**, and the
next phase — gate-motor integration against a Centurion D5/R5 per
[ADR-008](../decisions/ADR-008-gate-actuator-interface.md) — may start.

| # | Criterion | Proven in |
|---|---|---|
| E1 | Boots to a verified position from limit switches; faults closed on ambiguity | 1, 3 |
| E2 | DHCP + stable LAN presence; survives cable pulls without reboot | 2, 6 |
| E3 | Command round-trip (double-ack contract) drives relays; MotorTimeout and beam-interrupt behave per state machine | 3 |
| E4 | mTLS stream + https OTA against the site PKI on hardware; plaintext/wrong-CA peers excluded; cold-clock outcome recorded and (if failing) fix scheduled | 4 |
| E5 | Signed OTA installs and A/B-flips; tampered digest and bad signature both rejected on hardware | 5 |
| E6 | ≥8 h soak: zero unexpected resets, zero unexplained telemetry gaps, bounded reconnects, thermals stable | 6 |
| E7 | Current-draw and soak baselines recorded into the project log (they don't exist anywhere else) | 1, 6 |
| E8 | Known-risk register below dispositioned: each item fixed, scheduled, or explicitly accepted | — |

**Handoff note for motor integration:** the firmware's residential profile
drives two motion contactors and *rejects* `PULSE_RELAY`/`LATCH_*` commands
(`firmware/main/main.cpp`), while ADR-008 specifies a single dry-contact
*trigger pulse* into the Centurion controller (pulse-to-toggle). Reconciling
these — implement the pulse profile, or wire the D5/R5 behind the two-relay
profile — is the first decision of the next phase, **before** any wire lands
on a motor terminal.

## Known risks and open items

Found while grounding this plan; none block bench start, all need an owner.

| # | Item | Where | Proposed resolution |
|---|---|---|---|
| R1 | `CONFIG_GATE_SERVER_HOST` default `192.168.1.10` collides with the camera's address in the [Build Book IP plan](BUILD_BOOK.md#network-ip-address-plan) (server is `.50`) | `firmware/main/Kconfig.projbuild` | Set explicitly on bench (Stage 3); align the default or the IP plan in a follow-up |
| R2 | `CONFIG_GATE_OTA_MANIFEST_URL` default uses port `8080` (dashboard); nginx OTA serves `8081`/`8444` | Kconfig vs. `deployment/nginx/gate-ota.conf` | Same: set explicitly (Stages 4–5); fix the default |
| R3 | TLS handshake vs. epoch clock before first SNTP sync is undefined behaviour repo-wide | Stage 4.5 | Measure on bench; if failing, gate first connect on time sync; run LAN NTP (chrony on GPU host) regardless |
| R4 | ADR-008 pulse interface vs. firmware two-contactor profile (`PULSE_RELAY`/`LATCH_*` rejected) | `main.cpp` vs. ADR-008 | Decide at motor-integration kickoff (Stage 7 handoff note) |
| R5 | No remote fault-clear: `clear_fault()` has no wire command; field workaround is `REBOOT` | `gate_controller.hpp` / command dispatcher | Acceptable for bench; consider a `CLEAR_FAULT` command kind before field deployment |
| R6 | No current-draw or thermal baselines exist in the repo | — | Stages 1 and 6 record them; file them with the bench log |
| R7 | Bench uses the shared `gate-client.pem`; fleet needs one leaf per gate (SAN = gate id) for revocation | [`firmware/main/certs/README.md`](../../firmware/main/certs/README.md) | Fine single-gate; adopt per-gate leafs at first multi-gate site |
