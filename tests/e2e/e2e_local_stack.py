#!/usr/bin/env python3
"""e2e_local_stack.py — the Phase 4.9 multi-process integration pass.

Launches the real stack (no mocks) on loopback ports:

    gate-server        the actual daemon, CPU-only build, temp SQLite db
    gate-sim x2        virtual field controllers (fast physics)
    gate-dashboard     the Drogon bridge

and drives it from the outside — plain HTTP against the dashboard,
exactly like a browser — asserting the seams no single-process test
can cover:

    1. liveness      /api/health reports upstream up
    2. convergence   both gates appear in /api/status with live
                     telemetry, event stream connected
    3. allowlist     REST CRUD lands in the real AdminService + SQLite
                     and reads back
    4. command       POST open → gate physically travels
                     CLOSED → OPENING → OPEN (polled from telemetry),
                     then auto-closes back to CLOSED
    5. teardown      every process exits cleanly on SIGINT

Registered with ctest (see tests/CMakeLists.txt); binary paths arrive
as argv so the script needs no build-layout knowledge. Stdlib only —
no pip dependencies.
"""

import argparse
import json
import pathlib
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

GRPC_PORT = 58061
HTTP_PORT = 58080
BASE = f"http://127.0.0.1:{HTTP_PORT}"

procs = []


def launch(name, argv):
    p = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    procs.append((name, p))
    return p


def api(path, method="GET", body=None, timeout=5):
    req = urllib.request.Request(
        BASE + path,
        method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, json.loads(resp.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def wait_for(desc, predicate, timeout_s=30, interval_s=0.5):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            if predicate():
                print(f"  ok: {desc}")
                return
        except (urllib.error.URLError, ConnectionError, TimeoutError):
            pass
        time.sleep(interval_s)
    fail(f"timed out waiting for: {desc}")


def gate_state(status, gate_id):
    return status.get("gates", {}).get(gate_id, {}).get("telemetry", {}).get("state")


def fail(msg):
    print(f"FAIL: {msg}")
    for name, p in procs:
        if p.poll() is None:
            p.send_signal(signal.SIGKILL)
        out = (p.stdout.read() or "")[-2000:] if p.stdout else ""
        print(f"--- {name} (exit={p.poll()}) ---\n{out}")
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True)
    ap.add_argument("--sim", required=True)
    ap.add_argument("--dashboard", required=True)
    ap.add_argument("--tls", action="store_true",
                    help="run the whole stack under mTLS (Phase 4.10.1)")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        server_tls, client_tls = [], []
        if args.tls:
            print("== generating an ephemeral site PKI ==")
            gen = pathlib.Path(__file__).resolve().parents[2] / "scripts" / "gen-tls-certs.sh"
            subprocess.run(["bash", str(gen), f"{tmp}/tls"], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            server_tls = ["--tls-cert", f"{tmp}/tls/server.pem",
                          "--tls-key", f"{tmp}/tls/server.key",
                          "--tls-ca", f"{tmp}/tls/ca.pem", "--require-client-cert"]
            client_tls = ["--tls-ca", f"{tmp}/tls/ca.pem",
                          "--tls-cert", f"{tmp}/tls/gate-client.pem",
                          "--tls-key", f"{tmp}/tls/gate-client.key"]

        print(f"== launching the real stack{' (mTLS)' if args.tls else ''} ==")
        launch("gate-server", [args.server, "--listen", f"127.0.0.1:{GRPC_PORT}",
                               "--site-id", "site-e2e", "--db-path", f"{tmp}/allowlist.db",
                               "--log-level", "warn"] + server_tls)
        time.sleep(1.0)
        launch("sim-01", [args.sim, "--server", f"127.0.0.1:{GRPC_PORT}",
                          "--gate-id", "gate-e2e-01", "--travel-ms", "2000",
                          "--auto-close-ms", "4000", "--telemetry-ms", "250"] + client_tls)
        launch("sim-02", [args.sim, "--server", f"127.0.0.1:{GRPC_PORT}",
                          "--gate-id", "gate-e2e-02", "--travel-ms", "3000",
                          "--telemetry-ms", "250"] + client_tls)
        dash_tls = (["--tls-ca", f"{tmp}/tls/ca.pem",
                     "--tls-cert", f"{tmp}/tls/dashboard.pem",
                     "--tls-key", f"{tmp}/tls/dashboard.key"] if args.tls else [])
        launch("dashboard", [args.dashboard, "--listen", "127.0.0.1",
                             "--port", str(HTTP_PORT),
                             "--server", f"127.0.0.1:{GRPC_PORT}",
                             "--site-id", "site-e2e"] + dash_tls)

        if args.tls:
            # Enforcement checks: peers without the right credentials
            # must stay out — a plaintext sim and a TLS-but-certless
            # sim (mTLS requires the client certificate).
            launch("intruder-plain", [args.sim, "--server", f"127.0.0.1:{GRPC_PORT}",
                                      "--gate-id", "gate-intruder-plain",
                                      "--telemetry-ms", "250"])
            launch("intruder-nocert", [args.sim, "--server", f"127.0.0.1:{GRPC_PORT}",
                                       "--gate-id", "gate-intruder-nocert",
                                       "--telemetry-ms", "250",
                                       "--tls-ca", f"{tmp}/tls/ca.pem"])

        print("== 1: liveness ==")
        wait_for("dashboard answers /api/health with upstream up",
                 lambda: api("/api/health")[1].get("upstream") is True)

        print("== 2: convergence ==")

        def converged():
            _, s = api("/api/status")
            return (s.get("streamConnected")
                    and gate_state(s, "gate-e2e-01") == "CLOSED"
                    and gate_state(s, "gate-e2e-02") == "CLOSED")

        wait_for("both sims report CLOSED through the full pipeline", converged)

        if args.tls:
            print("== 2b: mTLS enforcement ==")
            time.sleep(3)  # ample time for an intruder to have converged if allowed
            _, s = api("/api/status")
            for intruder in ("gate-intruder-plain", "gate-intruder-nocert"):
                if intruder in s.get("gates", {}):
                    fail(f"{intruder} reached the dashboard despite bad credentials")
            print("  ok: plaintext and certless clients never reached the stack")

        print("== 3: allowlist CRUD against the real store ==")
        code, body = api("/api/allowlist", "POST",
                         {"plate": "KDA123X", "ownerName": "E2E Resident",
                          "ownerUnit": "B-12", "allowedClasses": ["SEDAN", "SUV"]})
        if code != 200 or body.get("inserted") != 1:
            fail(f"upsert: {code} {body}")
        print("  ok: upsert inserted=1")
        code, body = api("/api/allowlist")
        plates = [e["plate"] for e in body.get("entries", [])]
        if code != 200 or plates != ["KDA123X"]:
            fail(f"list after upsert: {code} {body}")
        print("  ok: list shows the entry (via AdminService + SQLite)")
        code, body = api("/api/allowlist/KDA123X", "DELETE")
        if code != 200:
            fail(f"delete: {code} {body}")
        code, body = api("/api/allowlist")
        if body.get("entries"):
            fail(f"list after delete not empty: {body}")
        print("  ok: delete roundtrip")

        print("== 4: command lifecycle — the gate physically travels ==")
        code, body = api("/api/gates/gate-e2e-01/command", "POST", {"kind": "OPEN_GATE"})
        if code != 200 or not body.get("ack", {}).get("commandId"):
            fail(f"issue open: {code} {body}")
        print(f"  ok: accepted ({body['ack']['commandId']})")
        wait_for("gate-e2e-01 reaches OPENING",
                 lambda: gate_state(api("/api/status")[1], "gate-e2e-01") == "OPENING",
                 timeout_s=10)
        wait_for("gate-e2e-01 reaches OPEN (travel complete)",
                 lambda: gate_state(api("/api/status")[1], "gate-e2e-01") == "OPEN",
                 timeout_s=10)
        wait_for("gate-e2e-01 auto-closes back to CLOSED",
                 lambda: gate_state(api("/api/status")[1], "gate-e2e-01") == "CLOSED",
                 timeout_s=15)
        # The untouched gate never moved.
        if gate_state(api("/api/status")[1], "gate-e2e-02") != "CLOSED":
            fail("gate-e2e-02 moved without a command")
        print("  ok: command addressed exactly one gate")

        print("== 5: clean teardown ==")
        for name, p in procs:
            p.send_signal(signal.SIGINT)
        for name, p in procs:
            try:
                code = p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill()
                fail(f"{name} did not exit on SIGINT")
            if code not in (0, -signal.SIGINT):
                fail(f"{name} exited with {code}")
            print(f"  ok: {name} exited cleanly")

    print("E2E PASS")


if __name__ == "__main__":
    main()
