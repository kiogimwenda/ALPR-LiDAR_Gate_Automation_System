#!/usr/bin/env bash
# issue-gate-cert.sh — issue a gate-bound field-controller leaf from the site CA.
#
# The shared gate-client.pem lets any holder speak as every gate. A
# production field controller gets its own leaf, CN gate-field-<gate-id>;
# gate-server then accepts its Control stream only for that gate_id
# (server/rpc/include/rpc/access_control.hpp), and one compromised
# board can be revoked by retiring one certificate.
#
# Usage:
#   issue-gate-cert.sh <pki-dir> <gate-id>
#   issue-gate-cert.sh site-tls gate-01   →  site-tls/gate-01.pem + gate-01.key
#
# Needs <pki-dir>/ca.pem + ca.key (from gen-tls-certs.sh). ECDSA P-256,
# the same as the rest of the site PKI, so the ESP32-S3's mbedTLS can use it.

set -euo pipefail

PKI="${1:?usage: issue-gate-cert.sh <pki-dir> <gate-id>}"
GATE="${2:?usage: issue-gate-cert.sh <pki-dir> <gate-id>}"
[[ "${GATE}" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "gate id must be [A-Za-z0-9._-]+: ${GATE}" >&2; exit 2; }
[[ -f "${PKI}/ca.pem" && -f "${PKI}/ca.key" ]] || { echo "no CA in ${PKI} (run gen-tls-certs.sh)" >&2; exit 1; }
[[ ! -e "${PKI}/${GATE}.key" ]] || { echo "refusing to overwrite ${PKI}/${GATE}.key" >&2; exit 1; }
umask 077

openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
    -keyout "${PKI}/${GATE}.key" -out "${PKI}/${GATE}.csr" -subj "/CN=gate-field-${GATE}"
openssl x509 -req -in "${PKI}/${GATE}.csr" -CA "${PKI}/ca.pem" -CAkey "${PKI}/ca.key" \
    -CAcreateserial -out "${PKI}/${GATE}.pem" -days 1095 \
    -extfile <(printf "basicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=clientAuth\nsubjectAltName=DNS:%s\n" "${GATE}")
rm -f "${PKI}/${GATE}.csr"

echo "issued ${PKI}/${GATE}.pem (CN gate-field-${GATE}) — install as the gate's"
echo "firmware/main/certs/gate.pem + gate.key (see firmware/main/certs/README.md)"
