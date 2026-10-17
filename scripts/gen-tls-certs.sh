#!/usr/bin/env bash
# gen-tls-certs.sh — site PKI for the gate stack (Phase 4.10.1).
#
# Generates a self-contained certificate authority for one site and
# issues, from it:
#
#   ca.pem / ca.key             the site CA (keep the key offline)
#   server.pem / server.key     gate-server's identity; SANs cover
#                               localhost + 127.0.0.1 + any extra
#                               names/IPs passed as arguments
#   dashboard.pem/.key          client identity for gate-dashboard
#                               (CN gate-dashboard: Dashboard + Admin RPCs)
#   vision.pem/.key             client identity for gate-vision
#                               (CN gate-vision: SubmitDetection only)
#   gate-client.pem/.key        shared field-controller identity for
#                               gate-sim and bench firmware (CN
#                               gate-field-client: Control, any gate)
#
# gate-server enforces those roles per RPC under --require-client-cert
# (server/rpc/include/rpc/access_control.hpp). For production, give each
# field controller its own gate-bound leaf with scripts/issue-gate-cert.sh.
#
# ECDSA P-256 throughout. Ed25519 would be smaller, but the ESP32-S3's
# mbedTLS cannot verify or present Ed25519 certificates in TLS, and the
# field controller must handshake with this same CA.
#
# Usage:
#   gen-tls-certs.sh <out-dir> [extra-san …]
#   gen-tls-certs.sh site-tls 192.168.1.10 gate-server.local

set -euo pipefail

OUT="${1:-./site-tls}"
shift || true

if [[ -e "${OUT}/ca.key" ]]; then
    echo "refusing to overwrite existing CA: ${OUT}/ca.key" >&2
    exit 1
fi
mkdir -p "${OUT}"
umask 077

SAN="DNS:localhost,IP:127.0.0.1"
for extra in "$@"; do
    if [[ "${extra}" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        SAN+=",IP:${extra}"
    else
        SAN+=",DNS:${extra}"
    fi
done

# CA (10 years — a site install, not the public web).
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout "${OUT}/ca.key" -out "${OUT}/ca.pem" \
    -subj "/CN=gate-site-ca" -days 3650 \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign"

issue() {
    local name="$1" cn="$2" eku="$3" san="${4:-}"
    openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout "${OUT}/${name}.key" \
        -out "${OUT}/${name}.csr" -subj "/CN=${cn}"
    local ext="basicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=${eku}"
    if [[ -n "${san}" ]]; then
        ext+="\nsubjectAltName=${san}"
    fi
    openssl x509 -req -in "${OUT}/${name}.csr" -CA "${OUT}/ca.pem" -CAkey "${OUT}/ca.key" \
        -CAcreateserial -out "${OUT}/${name}.pem" -days 1095 \
        -extfile <(printf "%b" "${ext}")
    rm -f "${OUT}/${name}.csr"
}

issue server "gate-server" "serverAuth" "${SAN}"
issue dashboard "gate-dashboard" "clientAuth"
issue vision "gate-vision" "clientAuth"
issue gate-client "gate-field-client" "clientAuth"

echo
echo "PKI written to ${OUT}/ (SANs: ${SAN})"
echo "  server:    --tls-cert server.pem --tls-key server.key --tls-ca ca.pem --require-client-cert"
echo "  dashboard: --tls-ca ca.pem --tls-cert dashboard.pem --tls-key dashboard.key"
echo "  vision:    --tls-ca ca.pem --tls-cert vision.pem --tls-key vision.key"
echo "  gate-sim:  --tls-ca ca.pem --tls-cert gate-client.pem --tls-key gate-client.key"
echo "  per-gate firmware leaf: scripts/issue-gate-cert.sh ${OUT} <gate-id>"
echo "keep ${OUT}/ca.key offline once all identities are issued."
