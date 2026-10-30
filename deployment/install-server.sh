#!/usr/bin/env bash
# install-server.sh — install/upgrade the GPU-host services.
#
# Copies the built binaries, systemd units, and (first run only) the env
# configs; keeps the previous binaries as *.prev for the manual rollback
# path described in gate-server.service. Operators use the Gate Control
# app (app/, .NET MAUI: Android, iOS, macOS, Windows) against
# gate-dashboard's API on port 8080 — there is no web UI to install.
#
# Usage (from a build on the GPU host):
#   sudo deployment/install-server.sh \
#       --build build/release \
#       --models /path/to/onnx-exports      # optional: build ALPR engines
#
# --models builds the TensorRT engines ON THIS HOST's GPU (any NVIDIA RTX
# 20-series or newer) into ${PREFIX}/models via
# scripts/export-models/build_engines.sh. Engines are GPU-generation and
# TensorRT-version specific, so re-run with --models after a GPU swap or a
# TensorRT upgrade; up-to-date engines are skipped.
#
# Idempotent: re-running upgrades binaries and never overwrites
# an edited /etc/gate/*.env.

set -euo pipefail

BUILD="" PREFIX="/opt/gate" MODELS=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)  BUILD="$2"; shift 2 ;;
        --www)    echo "--www is gone: the web dashboard was replaced by the Gate Control app (app/)" >&2; exit 2 ;;
        --prefix) PREFIX="$2"; shift 2 ;;
        --models) MODELS="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ -n "${BUILD}" ]] || { echo "required: --build <cmake build dir>" >&2; exit 2; }
[[ ${EUID} -eq 0 ]] || { echo "run as root (installs units + /opt)" >&2; exit 2; }

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Runtime prerequisite of the ALPR camera path: TensorRT's standard plugin
# library provides EfficientNMS_TRT, which the plate-detector engine contains
# (gate-vision dlopen()s it; without it the detector engine cannot load).
if [[ -x "${BUILD}/server/vision/gate-vision" ]] && ldd "${BUILD}/server/vision/gate-vision" 2>/dev/null | grep -q libnvinfer; then
    ldconfig -p | grep -q 'libnvinfer_plugin\.so\.10' || {
        echo "error: libnvinfer_plugin.so.10 not found — the ALPR plate detector needs TensorRT's" >&2
        echo "       standard plugins: apt install libnvinfer-plugin10 (or tensorrt-libs)" >&2
        exit 1
    }
fi

install_binary() {
    local src="$1" name="$2"
    [[ -x "${src}" ]] || { echo "missing binary: ${src}" >&2; exit 1; }
    if [[ -x "${PREFIX}/bin/${name}" ]]; then
        cp -f "${PREFIX}/bin/${name}" "${PREFIX}/bin/${name}.prev"
    fi
    install -D -m 0755 "${src}" "${PREFIX}/bin/${name}"
    echo "installed ${PREFIX}/bin/${name}"
}

# Service account + directories.
id -u gate >/dev/null 2>&1 || useradd --system --home-dir /var/lib/gate --shell /usr/sbin/nologin gate
install -d -m 0755 "${PREFIX}/bin"
install -d -m 0750 -o gate -g gate /var/lib/gate
install -d -m 0755 /srv/gate/firmware

# Binaries.
install_binary "${BUILD}/server/gate-server" gate-server
install_binary "${BUILD}/dashboard/backend/gate-dashboard" gate-dashboard
install_binary "${BUILD}/server/vision/gate-vision" gate-vision

# ALPR engines — built for this host's GPU (optional).
if [[ -n "${MODELS}" ]]; then
    install -d -m 0755 "${PREFIX}/models"
    install -m 0644 "${HERE}/../server/models/dict_kenya_plates.txt" "${PREFIX}/models/"
    "${HERE}/../scripts/export-models/build_engines.sh" --onnx-dir "${MODELS}" --out-dir "${PREFIX}/models"
fi

# Config — first install only; upgrades never clobber operator edits.
install -d -m 0755 /etc/gate
for env_file in gate-server.env gate-dashboard.env gate-vision.env; do
    if [[ ! -f "/etc/gate/${env_file}" ]]; then
        install -m 0644 "${HERE}/config/${env_file}" "/etc/gate/${env_file}"
        echo "installed default /etc/gate/${env_file} — review before first start"
    fi
done

# Units. gate-vision is installed but NOT enabled: it needs a
# configured source (camera + engines, or a scenario) in its env
# before it can start, unlike the self-sufficient server/dashboard.
install -m 0644 "${HERE}/systemd/gate-server.service" /etc/systemd/system/
install -m 0644 "${HERE}/systemd/gate-dashboard.service" /etc/systemd/system/
install -m 0644 "${HERE}/systemd/gate-vision.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable gate-server gate-dashboard

echo
echo "done. next steps:"
echo "  1. review /etc/gate/*.env"
echo "  2. systemctl restart gate-server gate-dashboard"
echo "     (then configure SOURCE_ARGS in gate-vision.env and"
echo "      systemctl enable --now gate-vision)"
echo "  3. OTA hosting: copy deployment/nginx/gate-ota.conf into nginx"
echo "     sites and publish signed manifests to /srv/gate/firmware"
echo "     (scripts/gen-ota-keys.sh + scripts/sign-ota-manifest.sh)"
