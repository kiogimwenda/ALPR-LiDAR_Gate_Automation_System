#!/usr/bin/env bash
# demo-stack.sh — one-command demo of the full gate stack (Phase 6.3).
#
# Launches, against a CPU-only build tree:
#
#   gate-server        the real daemon, temp SQLite allowlist
#   gate-sim x2        virtual field controllers with visible travel
#                      times (gate-demo-01 auto-closes)
#   gate-dashboard     REST/WebSocket bridge on :8080, serving the
#                      built SPA when dashboard/frontend/build exists
#   gate-vision        a looping scripted scenario against gate-demo-01:
#                      an unknown plate (denied) then the allowlisted
#                      demo plate (authorized → the gate auto-opens)
#
# so the dashboard shows a living site: periodic denials, authorized
# auto-opens, and a gate physically traveling CLOSED → OPEN → CLOSED.
#
# Usage:
#   scripts/demo-stack.sh                # plaintext, no admin auth
#   scripts/demo-stack.sh --hardened     # ephemeral mTLS PKI + JWT
#                                        # admin auth (demo credentials
#                                        # printed at startup)
#
#   BUILD_DIR=build/release-cpu scripts/demo-stack.sh
#
# Everything ephemeral (db, PKI, scenario, logs) lives in a mktemp dir
# that is removed — with every child process stopped — on Ctrl-C/exit.
# The demo needs nothing beyond the build outputs, curl, and python3;
# it never builds, installs, or writes into the repo.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build/debug-cpu}"
if [[ "${BUILD_DIR}" != /* ]]; then
    BUILD_DIR="${ROOT}/${BUILD_DIR}"
fi

GRPC_PORT="${GRPC_PORT:-58051}"
HTTP_PORT="${HTTP_PORT:-8080}"
BASE="http://127.0.0.1:${HTTP_PORT}"

DEMO_PLATE="KDA123X"
ADMIN_USER="demo-admin"
ADMIN_PASSWORD="demo-open-sesame"

HARDENED=0
for arg in "$@"; do
    case "${arg}" in
        --hardened) HARDENED=1 ;;
        -h|--help)
            sed -n '2,29p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown argument '${arg}' (only --hardened is supported)" >&2
            exit 1
            ;;
    esac
done

# ---------------------------------------------------------------- checks
for tool in curl python3 mktemp; do
    if ! command -v "${tool}" >/dev/null; then
        echo "error: '${tool}' is required for the demo" >&2
        exit 1
    fi
done

SERVER_BIN="${BUILD_DIR}/server/gate-server"
SIM_BIN="${BUILD_DIR}/simulation/gate-sim"
DASH_BIN="${BUILD_DIR}/dashboard/backend/gate-dashboard"
VISION_BIN="${BUILD_DIR}/server/vision/gate-vision"

missing=0
for bin in "${SERVER_BIN}" "${SIM_BIN}" "${DASH_BIN}" "${VISION_BIN}"; do
    if [[ ! -x "${bin}" ]]; then
        echo "missing binary: ${bin}" >&2
        missing=1
    fi
done
if [[ "${missing}" -ne 0 ]]; then
    cat >&2 <<EOF

The demo needs a built CPU tree (no GPU, no camera). Build it with:

    cmake --preset debug-cpu
    cmake --build --preset debug-cpu

then re-run this script. (BUILD_DIR=${BUILD_DIR})
EOF
    exit 1
fi

# ------------------------------------------------------------- lifecycle
TMP="$(mktemp -d)"
LOG="${TMP}/log"
mkdir -p "${LOG}"
PIDS=()

cleanup() {
    trap - EXIT INT TERM
    echo
    echo "== tearing down =="
    for pid in "${PIDS[@]}"; do
        kill -INT "${pid}" 2>/dev/null || true
    done
    for pid in "${PIDS[@]}"; do
        wait "${pid}" 2>/dev/null || true
    done
    rm -rf "${TMP}"
    echo "all demo processes stopped, temp state removed."
}
trap cleanup EXIT
trap 'exit 130' INT TERM

launch() { # launch <name> <argv…>   — logs to $LOG/<name>.log
    local name="$1"
    shift
    "$@" >"${LOG}/${name}.log" 2>&1 &
    PIDS+=("$!")
    echo "  ${name} (pid $!)"
}

api() { # api <method> <path> [json-body]
    local method="$1" path="$2" body="${3:-}"
    local args=(-fsS -X "${method}" -H "Content-Type: application/json")
    if [[ -n "${TOKEN:-}" ]]; then
        args+=(-H "Authorization: Bearer ${TOKEN}")
    fi
    if [[ -n "${body}" ]]; then
        args+=(-d "${body}")
    fi
    curl "${args[@]}" "${BASE}${path}"
}

# ------------------------------------------------------ hardened mode PKI
SERVER_TLS=()
CLIENT_TLS=()
DASH_TLS=()
DASH_AUTH=()
if [[ "${HARDENED}" -eq 1 ]]; then
    echo "== generating an ephemeral site PKI (--hardened) =="
    bash "${ROOT}/scripts/gen-tls-certs.sh" "${TMP}/tls" >"${LOG}/gen-tls-certs.log" 2>&1
    SERVER_TLS=(--tls-cert "${TMP}/tls/server.pem" --tls-key "${TMP}/tls/server.key"
                --tls-ca "${TMP}/tls/ca.pem" --require-client-cert)
    CLIENT_TLS=(--tls-ca "${TMP}/tls/ca.pem"
                --tls-cert "${TMP}/tls/gate-client.pem" --tls-key "${TMP}/tls/gate-client.key")
    DASH_TLS=(--tls-ca "${TMP}/tls/ca.pem"
              --tls-cert "${TMP}/tls/dashboard.pem" --tls-key "${TMP}/tls/dashboard.key")

    # scripts/gen-admin-hash.sh prompts on stdin by design (a real
    # credential should never sit in argv or an env file); for a demo
    # credential we mint the same pbkdf2-sha256$iter$salt$hash format
    # inline, exactly like the E2E harness does.
    ADMIN_HASH="$(PW="${ADMIN_PASSWORD}" python3 - <<'EOF'
import hashlib, os, secrets
salt = secrets.token_bytes(16)
key = hashlib.pbkdf2_hmac("sha256", os.environ["PW"].encode(), salt, 50000, dklen=32)
print(f"pbkdf2-sha256$50000${salt.hex()}${key.hex()}")
EOF
)"
    DASH_AUTH=(--admin-user "${ADMIN_USER}" --admin-password-hash "${ADMIN_HASH}"
               --token-ttl-min 60)
fi

# ------------------------------------------------------------ the stack
MODE_NOTE=""
if [[ "${HARDENED}" -eq 1 ]]; then
    MODE_NOTE=" (mTLS + admin auth)"
fi
echo "== launching the demo stack${MODE_NOTE} =="
launch gate-server "${SERVER_BIN}" --listen "127.0.0.1:${GRPC_PORT}" \
    --site-id site-demo --db-path "${TMP}/allowlist.db" --log-level warn \
    "${SERVER_TLS[@]}"
sleep 1

launch sim-gate-demo-01 "${SIM_BIN}" --server "127.0.0.1:${GRPC_PORT}" \
    --gate-id gate-demo-01 --travel-ms 4000 --auto-close-ms 6000 \
    --telemetry-ms 250 "${CLIENT_TLS[@]}"
launch sim-gate-demo-02 "${SIM_BIN}" --server "127.0.0.1:${GRPC_PORT}" \
    --gate-id gate-demo-02 --travel-ms 5000 --telemetry-ms 250 "${CLIENT_TLS[@]}"

SPA_DIR="${ROOT}/dashboard/frontend/build"
DASH_WWW=()
SPA_NOTE="  (the built SPA, served same-origin)"
if [[ -f "${SPA_DIR}/index.html" ]]; then
    DASH_WWW=(--www "${SPA_DIR}")
else
    SPA_NOTE="  (API only — build the SPA first: cd dashboard/frontend && npm ci && npm run build)"
fi
launch gate-dashboard "${DASH_BIN}" --listen 127.0.0.1 --port "${HTTP_PORT}" \
    --server "127.0.0.1:${GRPC_PORT}" --site-id site-demo \
    "${DASH_WWW[@]}" "${DASH_TLS[@]}" "${DASH_AUTH[@]}"

echo "== waiting for the dashboard to see the gate-server =="
for _ in $(seq 1 60); do
    if curl -fsS "${BASE}/api/health" 2>/dev/null | grep -q '"upstream"[[:space:]]*:[[:space:]]*true'; then
        break
    fi
    sleep 0.5
done
if ! curl -fsS "${BASE}/api/health" 2>/dev/null | grep -q '"upstream"[[:space:]]*:[[:space:]]*true'; then
    echo "error: the stack never converged; logs are in ${LOG}" >&2
    tail -n 20 "${LOG}"/*.log >&2 || true
    exit 1
fi
echo "  ok: /api/health reports upstream up"

# ---------------------------------------------------------- demo content
TOKEN=""
if [[ "${HARDENED}" -eq 1 ]]; then
    TOKEN="$(api POST /api/auth/login \
        "{\"username\": \"${ADMIN_USER}\", \"password\": \"${ADMIN_PASSWORD}\"}" \
        | python3 -c 'import json,sys; print(json.load(sys.stdin)["token"])')"
    echo "  ok: logged in as ${ADMIN_USER} (JWT minted)"
fi

echo "== seeding the allowlist =="
api POST /api/allowlist "{\"plate\": \"${DEMO_PLATE}\", \"ownerName\": \"Demo Resident\",
    \"ownerUnit\": \"D-1\", \"allowedClasses\": [\"SEDAN\", \"SUV\"]}" >/dev/null
echo "  ${DEMO_PLATE} (Demo Resident, unit D-1, SEDAN + SUV) — the plate the demo loop authorizes"

# One iteration ≈ 20 s: a denied unknown plate at t=0, the allowlisted
# plate at t=4 s (auto-opens gate-demo-01: 4 s travel, 6 s dwell, 4 s
# close), then a quiet tail so the gate is CLOSED again before the next
# lap. 50 laps ≈ 17 minutes of living demo.
cat >"${TMP}/scenario.json" <<EOF
{
  "loop": 50,
  "events": [
    {
      "offset_ms": 0,
      "plates": [{"text": "KDX999Z", "detection_conf": 0.91, "ocr_conf": 0.88}],
      "vehicles": [{"class": "sedan", "confidence": 0.90}]
    },
    {
      "offset_ms": 4000,
      "plates": [{"text": "${DEMO_PLATE}", "detection_conf": 0.95, "ocr_conf": 0.93}],
      "vehicles": [{"class": "suv", "confidence": 0.92}]
    },
    {"offset_ms": 20000}
  ]
}
EOF

echo "== launching gate-vision (scripted scenario against gate-demo-01) =="
launch gate-vision "${VISION_BIN}" --server "127.0.0.1:${GRPC_PORT}" \
    --site-id site-demo --gate-id gate-demo-01 \
    --scenario "${TMP}/scenario.json" "${CLIENT_TLS[@]}"

# ---------------------------------------------------------------- banner
cat <<EOF

========================================================================
  gate-automation demo is up  (site-demo, 2 gates, looping detections)

  dashboard  ${BASE}/${SPA_NOTE}
  status     ${BASE}/api/status
  health     ${BASE}/api/health
  logs       ${LOG}/

  every ~20 s on gate-demo-01:
    KDX999Z  → denied (not on the allowlist)
    ${DEMO_PLATE}  → authorized → OPEN_GATE auto-dispatched → the gate
               travels CLOSED → OPENING (4 s) → OPEN, dwells 6 s,
               then auto-closes. gate-demo-02 stays idle.
EOF
if [[ "${HARDENED}" -eq 1 ]]; then
    cat <<EOF

  hardened mode: gRPC is mTLS-only; allowlist edits and gate commands
  need a login. Demo admin credentials:
      username  ${ADMIN_USER}
      password  ${ADMIN_PASSWORD}
EOF
fi
cat <<EOF

  Ctrl-C to tear everything down.
========================================================================

EOF

wait
