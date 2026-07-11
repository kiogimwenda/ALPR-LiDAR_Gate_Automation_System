#!/usr/bin/env bash
# gen-admin-hash.sh — mint the dashboard admin credential (4.10.2).
#
# Prompts for a password (never echoed, never in argv or shell
# history) and prints the pbkdf2-sha256$<iter>$<salt>$<hash> string
# that gate-dashboard's --admin-password-hash expects. PBKDF2 via
# python3's hashlib — the same stdlib the E2E harness already
# depends on, and bit-identical to the backend's OpenSSL
# PKCS5_PBKDF2_HMAC.
#
#   ./scripts/gen-admin-hash.sh            # interactive prompt
#   ITERATIONS=500000 ./scripts/gen-admin-hash.sh
set -euo pipefail

ITERATIONS="${ITERATIONS:-210000}"

read -rsp "admin password: " pw1; echo >&2
read -rsp "again: " pw2; echo >&2
if [[ "${pw1}" != "${pw2}" ]]; then
    echo "error: passwords do not match" >&2
    exit 1
fi
if [[ -z "${pw1}" ]]; then
    echo "error: empty password" >&2
    exit 1
fi

PW="${pw1}" python3 - "${ITERATIONS}" <<'EOF'
import hashlib, os, secrets, sys

iterations = int(sys.argv[1])
password = os.environ["PW"].encode()
salt = secrets.token_bytes(16)
key = hashlib.pbkdf2_hmac("sha256", password, salt, iterations, dklen=32)
print(f"pbkdf2-sha256${iterations}${salt.hex()}${key.hex()}")
EOF
