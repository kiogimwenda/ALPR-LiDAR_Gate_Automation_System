#!/usr/bin/env bash
# sign-ota-manifest.sh — build and sign an OTA manifest (ADR-009).
#
# Takes a firmware image and emits the manifest.json the gate_ota
# component consumes (schema documented in
# firmware/components/gate_ota/include/gate_ota/ota_manifest.hpp).
#
# Signature convention — must match OtaUpdater::run_inner exactly:
# the ed25519 signature is over the raw 32-byte SHA-256 digest of the
# image (the same digest the firmware recomputes from a flash
# readback), not over the file. openssl's -rawin signs its input as
# the ed25519 message, which is precisely that.
#
# Usage:
#   sign-ota-manifest.sh --bin build/gate_firmware.bin \
#                        --version 0.2.0 \
#                        --url http://192.168.1.10:8081/firmware/gate_firmware.bin \
#                        --key ota-keys/ota-signing.key \
#                        [--key-id deploy-2026] [--min-uptime 60] \
#                        [--out manifest.json]
#
# The script verifies its own signature before writing anything.

set -euo pipefail

BIN="" VERSION="" URL="" KEY="" KEY_ID="deploy" MIN_UPTIME=60 OUT="manifest.json"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --bin)        BIN="$2"; shift 2 ;;
        --version)    VERSION="$2"; shift 2 ;;
        --url)        URL="$2"; shift 2 ;;
        --key)        KEY="$2"; shift 2 ;;
        --key-id)     KEY_ID="$2"; shift 2 ;;
        --min-uptime) MIN_UPTIME="$2"; shift 2 ;;
        --out)        OUT="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ -f "${BIN}" && -n "${VERSION}" && -n "${URL}" && -f "${KEY}" ]] || {
    echo "required: --bin <file> --version <semver> --url <http(s)://…> --key <pem>" >&2
    exit 2
}

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

SIZE="$(stat -c %s "${BIN}")"
SHA_HEX="$(sha256sum "${BIN}" | cut -d' ' -f1)"

# Raw digest bytes are the ed25519 message (openssl computes them
# directly — no hex round-trip, no xxd dependency).
openssl dgst -sha256 -binary "${BIN}" > "${WORK}/digest.bin"
openssl pkeyutl -sign -rawin -inkey "${KEY}" \
    -in "${WORK}/digest.bin" -out "${WORK}/sig.bin"
SIG_HEX="$(od -An -tx1 "${WORK}/sig.bin" | tr -d ' \n')"

# Self-check before publishing: the public half must accept what the
# firmware will be asked to accept.
openssl pkey -in "${KEY}" -pubout -out "${WORK}/pub.pem"
openssl pkeyutl -verify -rawin -pubin -inkey "${WORK}/pub.pem" \
    -in "${WORK}/digest.bin" -sigfile "${WORK}/sig.bin" > /dev/null

cat > "${OUT}" <<EOF
{
  "target_version": "${VERSION}",
  "image_url": "${URL}",
  "image_size": ${SIZE},
  "image_sha256": "${SHA_HEX}",
  "ed25519_signature": "${SIG_HEX}",
  "signing_key_id": "${KEY_ID}",
  "min_uptime_sec": ${MIN_UPTIME}
}
EOF

echo "manifest written to ${OUT} (${VERSION}, ${SIZE} bytes, key_id=${KEY_ID})"
