#!/usr/bin/env bash
# sign-ota-manifest.sh — build and sign an OTA manifest (ADR-009).
#
# Takes a firmware image and emits the manifest.json the gate_ota
# component consumes (schema documented in
# firmware/components/gate_ota/include/gate_ota/ota_manifest.hpp).
#
# Signature convention (scheme gate-ota/v1) — must match
# gate::ota::signed_message() exactly. The ed25519 message is the
# statement
#
#     gate-ota/v1\n<version>\n<size>\n<sha256 hex>\n
#
# binding the image digest (which the firmware recomputes from a flash
# readback) to its version and size. The firmware also refuses any
# version that is not newer than the running one, so an old signed
# manifest cannot be replayed to downgrade a gate.
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

[[ "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$ ]] || {
    echo "--version must be MAJOR.MINOR.PATCH[-pre][+build]: ${VERSION}" >&2
    exit 2
}
(( ${#VERSION} < 24 )) || { echo "--version longer than the firmware's 23-char field" >&2; exit 2; }

printf 'gate-ota/v1\n%s\n%s\n%s\n' "${VERSION}" "${SIZE}" "${SHA_HEX}" > "${WORK}/statement.bin"
openssl pkeyutl -sign -rawin -inkey "${KEY}" \
    -in "${WORK}/statement.bin" -out "${WORK}/sig.bin"
SIG_HEX="$(od -An -tx1 "${WORK}/sig.bin" | tr -d ' \n')"

# Self-check before publishing: the public half must accept what the
# firmware will be asked to accept.
openssl pkey -in "${KEY}" -pubout -out "${WORK}/pub.pem"
openssl pkeyutl -verify -rawin -pubin -inkey "${WORK}/pub.pem" \
    -in "${WORK}/statement.bin" -sigfile "${WORK}/sig.bin" > /dev/null

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
