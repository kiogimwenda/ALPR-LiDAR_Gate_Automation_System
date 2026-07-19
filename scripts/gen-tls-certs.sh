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
#   gate-client.pem/.key        client identity for gate-sim (and the
#                               firmware once 4.10.3 lands)
#
# ed25519 throughout — small, fast, no parameter choices to get wrong.
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
openssl req -x509 -newkey ed25519 -nodes -keyout "${OUT}/ca.key" -out "${OUT}/ca.pem" \
    -subj "/CN=gate-site-ca" -days 3650 \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign"

issue() {
    local name="$1" cn="$2" eku="$3" san="${4:-}"
    openssl req -newkey ed25519 -nodes -keyout "${OUT}/${name}.key" \
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
issue gate-client "gate-field-client" "clientAuth"

echo
echo "PKI written to ${OUT}/ (SANs: ${SAN})"
echo "  server:    --tls-cert server.pem --tls-key server.key --tls-ca ca.pem --require-client-cert"
echo "  dashboard: --tls-ca ca.pem --tls-cert dashboard.pem --tls-key dashboard.key"
echo "  gate-sim:  --tls-ca ca.pem --tls-cert gate-client.pem --tls-key gate-client.key"
echo "keep ${OUT}/ca.key offline once all identities are issued."
