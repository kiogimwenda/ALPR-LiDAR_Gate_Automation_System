#!/usr/bin/env bash
# gen-ota-keys.sh — generate the ADR-009 OTA signing keypair.
#
# Produces:
#   <out>/ota-signing.key      ed25519 private key (PEM) — KEEP OFFLINE.
#                              ADR-009: never on the device or the
#                              update server; it exists only where
#                              releases are signed.
#   <out>/ota-signing.pub      public key (PEM), for sign-time verify
#   <out>/ota-pubkey.hex       raw 32-byte public key as 64 hex chars —
#                              paste into the firmware's menuconfig
#                              (Gate Firmware Configuration →
#                              "OTA ed25519 public key").
#
# Usage: gen-ota-keys.sh [output-dir]   (default: ./ota-keys)

set -euo pipefail

OUT="${1:-./ota-keys}"
KEY="${OUT}/ota-signing.key"

if [[ -e "${KEY}" ]]; then
    echo "refusing to overwrite existing key: ${KEY}" >&2
    exit 1
fi

mkdir -p "${OUT}"
umask 077

openssl genpkey -algorithm ed25519 -out "${KEY}"
openssl pkey -in "${KEY}" -pubout -out "${OUT}/ota-signing.pub"

# The DER SubjectPublicKeyInfo for ed25519 is a fixed 12-byte header
# followed by the raw 32-byte key; tail extracts the raw key. od+tr
# instead of xxd — coreutils only, works on any deploy host.
openssl pkey -in "${KEY}" -pubout -outform DER \
    | tail -c 32 | od -An -tx1 | tr -d ' \n' > "${OUT}/ota-pubkey.hex"
echo >> "${OUT}/ota-pubkey.hex"

echo "keypair written to ${OUT}/"
echo
echo "firmware provisioning (idf.py menuconfig → Gate Firmware Configuration):"
echo "  GATE_OTA_PUBKEY = $(cat "${OUT}/ota-pubkey.hex")"
echo
echo "store ${KEY} OFFLINE — it must never reach a device or the update server."
