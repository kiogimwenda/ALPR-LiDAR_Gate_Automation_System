#!/usr/bin/env bash
# 04-vcpkg.sh — Install and configure vcpkg package manager.
# Decision rationale: see docs/decisions/ADR-001-package-manager.md
set -euo pipefail

echo "=== ALPR+LiDAR Gate Automation — vcpkg Bootstrap ==="

VCPKG_DIR="${HOME}/vcpkg"

if [ -x "$VCPKG_DIR/vcpkg" ]; then
    echo "vcpkg already installed at $VCPKG_DIR"
    "$VCPKG_DIR/vcpkg" version | head -1
else
    echo "[1/2] Cloning vcpkg..."
    sudo apt-get install -y curl zip unzip tar
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"

    echo "[2/2] Bootstrapping vcpkg..."
    "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
fi

# Ensure VCPKG_ROOT is set
if ! grep -q 'VCPKG_ROOT' ~/.bashrc 2>/dev/null; then
    echo "export VCPKG_ROOT=$VCPKG_DIR" >> ~/.bashrc
    echo 'export PATH="$VCPKG_ROOT:$PATH"' >> ~/.bashrc
    echo "Added VCPKG_ROOT to ~/.bashrc"
fi

export VCPKG_ROOT="$VCPKG_DIR"
echo "VCPKG_ROOT=$VCPKG_ROOT"
echo ""
echo "=== vcpkg bootstrap complete ==="
