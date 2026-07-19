# Demo: the full stack in one command

`scripts/demo-stack.sh` launches the entire system — real server, real
dashboard, virtual gates, scripted vision — on loopback, with nothing
installed and nothing written outside a temp directory. It is the
fastest way to *see* what this project does: a vehicle is detected, its
plate is checked against the allowlist, and the gate opens by itself.

## Prerequisites

- A **CPU-only** build tree. No GPU, no TensorRT, no camera:

  ```sh
  cmake --preset debug-cpu
  cmake --build --preset debug-cpu
  ```

- `curl` and `python3` (already required by the test suite).
- Optional, for the browser UI: the built SPA at
  `dashboard/frontend/build` (`cd dashboard/frontend && npm ci && npm
  run build`). Without it the demo still runs — you just watch the JSON
  API instead of the web page.

## The one command

```sh
scripts/demo-stack.sh
```

(`BUILD_DIR=build/release-cpu scripts/demo-stack.sh` to point at a
different preset's tree. Ctrl-C tears everything down — processes,
database, logs, all of it.)

## What appears, and why it matters

The script brings up `gate-server` (site `site-demo`, temp SQLite
allowlist), two `gate-sim` virtual field controllers (`gate-demo-01`
with a visible 4 s travel / 6 s auto-close dwell, and an idle
`gate-demo-02`), the dashboard on <http://127.0.0.1:8080/>, seeds the
allowlist with the demo plate **KDA123X** (SEDAN + SUV), and starts
`gate-vision` replaying a looping scripted scenario at `gate-demo-01`.

Every ~20 seconds, for about 17 minutes:

1. **KDX999Z** is "seen" → not on the allowlist → **DENIED**. Nothing
   moves. Denials are the system's default posture.
2. **KDA123X** is "seen" → plate + vehicle class match →
   **AUTHORIZED** → the server auto-dispatches `OPEN_GATE` → the gate
   travels `CLOSED → OPENING → OPEN`, dwells, and auto-closes.

That second step — *a detection opening a gate with no human in the
loop* — is the product. Everything else in the repo (fusion, auth,
mTLS, OTA, hardware) exists to make that one transition trustworthy.
Watch it live on the dashboard, or poll it:

```sh
curl -s http://127.0.0.1:8080/api/status | python3 -m json.tool
```

`gate-demo-02` never moves: authorization is per-gate, and nobody has
authorized anything at gate 2.

## Things to try

| Try this | What you'll see |
| --- | --- |
| `curl -X POST -H 'Content-Type: application/json' -d '{"kind":"OPEN_GATE"}' http://127.0.0.1:8080/api/gates/gate-demo-02/command` (or the dashboard's gate controls) | The idle gate travels the full open/close cycle on operator command — the manual path alongside the automatic one. |
| `curl -X DELETE http://127.0.0.1:8080/api/allowlist/KDA123X` | Within one scenario lap, KDA123X's verdict flips to DENIED and the gate stops opening. Re-add it (`curl -X POST -H 'Content-Type: application/json' -d '{"plate":"KDA123X","ownerName":"Demo Resident","ownerUnit":"D-1","allowedClasses":["SEDAN","SUV"]}' http://127.0.0.1:8080/api/allowlist`) and the auto-opens resume. Live policy, no restarts. |
| Re-run as `scripts/demo-stack.sh --hardened` | An ephemeral site PKI is minted (`scripts/gen-tls-certs.sh`); every gRPC hop is mTLS; the admin surface locks: `curl -i http://127.0.0.1:8080/api/allowlist` → **401**, then log in (`curl -X POST -H 'Content-Type: application/json' -d '{"username":"demo-admin","password":"demo-open-sesame"}' http://127.0.0.1:8080/api/auth/login`) and retry with `-H "Authorization: Bearer <token>"` → **200**. Monitoring (`/api/status`, `/api/health`) stays open by design. The SPA login page drives the same flow. |
| In a second terminal (plaintext demo): `build/debug-cpu/simulation/gate-sim --server 127.0.0.1:58051 --gate-id gate-demo-03 --travel-ms 4000 --interactive` | A third gate appears on the dashboard, hand-driven from your keyboard: `open`, `close`, `stop`, `trip` (safety beam), `clear`, `status`, `quit`. Watch the reverse-on-beam policy by typing `trip` while it is closing. |

## What this demo is NOT

- **No GPU inference.** `gate-vision` here replays a scripted scenario
  file; the TensorRT ALPR pipeline (YOLOv9 plate detector + PaddleOCR
  recognizer) only exists in `ENABLE_GPU=ON` builds with a real camera
  or video source.
- **No real camera or LiDAR.** Detections are synthetic, and the gates
  are simulated physics, not motors. The path from this demo to real
  sensing hardware is documented in the
  [bench validation plan](hardware/11-bench-validation-plan.md) and
  [ADR-012 — fusion sensing hardware](decisions/ADR-012-fusion-sensing-hardware.md).

Same pipeline, same server, same decisions — only the two ends
(sensor in, motor out) are stand-ins.
