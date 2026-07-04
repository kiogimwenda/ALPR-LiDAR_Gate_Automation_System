#!/usr/bin/env bash
# gen-nanopb.sh — regenerate the firmware's nanopb protobuf sources.
#
# Output is committed (firmware/components/gate_rpc/src/pb/) so CI and
# clean checkouts build without a Python toolchain; run this after any
# change to shared/proto/gate_service.proto or gate_service.options.
#
# The generator version is pinned to match the nanopb runtime the
# firmware links (livekit/nanopb on the ESP component registry — see
# firmware/components/gate_rpc/idf_component.yml and ADR-011). A
# generator/runtime mismatch fails the firmware build at compile time
# via NANOPB_VERSION checks in the generated headers.

set -euo pipefail

NANOPB_VERSION=0.4.9

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROTO_DIR="${REPO_ROOT}/shared/proto"
OUT_DIR="${REPO_ROOT}/firmware/components/gate_rpc/src/pb"
VENV_DIR="${REPO_ROOT}/build/nanopb-venv"

# Well-known types (timestamp/duration/empty) are resolved from the
# system protobuf includes; nanopb ships its own copies too, but using
# the system set keeps us aligned with the protoc the server uses.
WKT_INCLUDE=/usr/include

if [[ ! -d "${VENV_DIR}" ]]; then
    python3 -m venv "${VENV_DIR}"
fi
# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"
pip install --quiet --retries 10 "nanopb==${NANOPB_VERSION}" protobuf

mkdir -p "${OUT_DIR}"

# Run from the proto directory: the nanopb plugin resolves the
# gate_service.options sizing file relative to the working directory.
cd "${PROTO_DIR}"
protoc \
    --plugin=protoc-gen-nanopb="${VENV_DIR}/bin/protoc-gen-nanopb" \
    -I. -I"${WKT_INCLUDE}" \
    --nanopb_out="${OUT_DIR}" \
    gate_service.proto \
    "${WKT_INCLUDE}/google/protobuf/timestamp.proto" \
    "${WKT_INCLUDE}/google/protobuf/duration.proto" \
    "${WKT_INCLUDE}/google/protobuf/empty.proto"

echo "nanopb sources regenerated in ${OUT_DIR}"
