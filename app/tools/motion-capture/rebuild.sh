#!/bin/bash
# rebuild.sh <shot.png> — stop, build (Windows, Debug), relaunch at W×H px and screenshot into $OUT_DIR.
WS="$(wslpath -w "$(dirname "$(readlink -f "$0")")")"
OUT="$(wslpath -w "${OUT_DIR:-/tmp}")"
cd /mnt/c
powershell.exe -NoProfile -Command "Get-Process GateControl -ErrorAction SilentlyContinue | Stop-Process -Force"
timeout 900 "/mnt/c/Program Files/dotnet/dotnet.exe" build 'W:\home\deby\projects\gate-automation\app\src\GateControl.App' -f net10.0-windows10.0.19041.0 -v q -nologo 2>&1 | grep -E " error |warning CS|Error\(s\)" | sort -u | head
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WS\\launch.ps1" -Width ${W:-1500} -Height ${H:-960} | tail -1
sleep 7
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WS\\shot.ps1" -Out "$OUT\\$1" | tail -1
