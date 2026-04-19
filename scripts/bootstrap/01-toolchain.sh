#!/usr/bin/env bash
# 01-toolchain.sh — Install core build toolchain for the ALPR+LiDAR project.
# Idempotent: safe to re-run on a fresh WSL2 Debian image.
set -euo pipefail

echo "=== ALPR+LiDAR Gate Automation — Toolchain Bootstrap ==="

sudo apt-get update -qq

# Core build tools
PACKAGES=(
    gcc g++
    clang clang-tools
    cmake
    ninja-build
    mold
    ccache
    pkg-config
    git git-lfs
    curl wget zip unzip tar
    build-essential
)

echo "[1/3] Installing core build packages..."
sudo apt-get install -y "${PACKAGES[@]}"

# Quality gate tools
QUALITY_PACKAGES=(
    clang-format
    clang-tidy
    cppcheck
    iwyu
)

echo "[2/3] Installing quality gate tools..."
sudo apt-get install -y "${QUALITY_PACKAGES[@]}"

# Other dev tools
DEV_PACKAGES=(
    sqlite3
    libsqlite3-dev
    python3
    python3-pip
    python3-venv
)

echo "[3/3] Installing dev tools..."
sudo apt-get install -y "${DEV_PACKAGES[@]}"

echo ""
echo "=== Toolchain versions ==="
echo "GCC:          $(gcc --version | head -1)"
echo "Clang:        $(clang --version | head -1)"
echo "CMake:        $(cmake --version | head -1)"
echo "Ninja:        $(ninja --version)"
echo "mold:         $(mold --version | head -1)"
echo "ccache:       $(ccache --version | head -1)"
echo "git:          $(git --version)"
echo "git-lfs:      $(git lfs version)"
echo "sqlite3:      $(sqlite3 --version)"
echo "clang-format: $(clang-format --version)"
echo "clang-tidy:   $(clang-tidy --version | head -1)"
echo "cppcheck:     $(cppcheck --version)"
echo "iwyu:         $(include-what-you-use --version 2>&1 | head -1)"
echo "Python:       $(python3 --version)"
echo ""
echo "=== Toolchain bootstrap complete ==="
