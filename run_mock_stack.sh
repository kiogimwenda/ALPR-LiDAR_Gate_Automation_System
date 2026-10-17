#!/usr/bin/env bash
set -e

echo "=== ALPR+LiDAR Mock Stack Setup & Runner ==="
echo "Working directory: $(pwd)"

# 1. Dependency checks
missing_pkgs=()

if ! command -v cmake &> /dev/null; then
    missing_pkgs+=(cmake ninja-build mold pkg-config build-essential)
fi

if [ ${#missing_pkgs[@]} -gt 0 ]; then
    echo "Missing dependencies detected. Installing: ${missing_pkgs[*]}"
    sudo apt-get update -qq
    sudo apt-get install -y "${missing_pkgs[@]}"
fi

# 2. vcpkg bootstrap
if [ -z "${VCPKG_ROOT:-}" ] || [ ! -d "$VCPKG_ROOT" ]; then
    echo "VCPKG_ROOT is not set or not found. Checking if vcpkg bootstrap is needed..."
    if [ -f "./scripts/bootstrap/04-vcpkg.sh" ]; then
        ./scripts/bootstrap/04-vcpkg.sh
    else
        echo "Warning: vcpkg bootstrap script not found at ./scripts/bootstrap/04-vcpkg.sh"
    fi
    # The script installs vcpkg to ~/vcpkg by default
    export VCPKG_ROOT="$HOME/vcpkg"
    export PATH="$VCPKG_ROOT:$PATH"
fi

# 3. Build C++ project
echo "Configuring CMake (debug-cpu)..."
cmake --preset debug-cpu

echo "Building C++ binaries..."
cmake --build --preset debug-cpu

# 5. Run the stack
echo ""
echo "=== Starting Mock Stack ==="

# Cleanup function to kill all background jobs when the script exits
cleanup() {
    echo ""
    echo "Stopping all services..."
    [ -n "${DASH_BACKEND_PID:-}" ] && kill $DASH_BACKEND_PID 2>/dev/null || true
    [ -n "${SIM_PID:-}" ] && kill $SIM_PID 2>/dev/null || true
    [ -n "${MOCK_SERVER_PID:-}" ] && kill $MOCK_SERVER_PID 2>/dev/null || true
    exit 0
}

# Trap CTRL+C and termination signals
trap cleanup SIGINT SIGTERM EXIT

BUILD_DIR="build/debug-cpu"

# Start the Mock Server
echo "Starting gate-mock-server on port 50051..."
./${BUILD_DIR}/simulation/gate-mock-server --listen 127.0.0.1:50051 --decisions-period 15 &
MOCK_SERVER_PID=$!

sleep 1

# Start the Gate Simulator
echo "Starting gate-sim for gate-01..."
./${BUILD_DIR}/simulation/gate-sim --server 127.0.0.1:50051 --gate-id gate-01 &
SIM_PID=$!

# Start the Dashboard Backend
echo "Starting gate-dashboard backend on port 8080..."
./${BUILD_DIR}/dashboard/backend/gate-dashboard --listen 127.0.0.1 --port 8080 --server 127.0.0.1:50051 \
    --history-db "${BUILD_DIR}/mock-history.db" &
DASH_BACKEND_PID=$!

echo "======================================================"
echo " All services are running! "
echo " Open the Gate Control app (app/GateControl.slnx) and sign in to"
echo " server 127.0.0.1:8080 (no account needed: open access)."
echo " Press Ctrl+C to stop all services and exit."
echo "======================================================"

# Wait indefinitely for the background processes
wait
