#!/bin/bash
# signin.sh — sign the running Windows app in to the demo stack via UI Automation (no mouse).
WS="$(wslpath -w "$(dirname "$(readlink -f "$0")")")"
cd /mnt/c
P(){ powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WS\\$1" "${@:2}" 2>&1 | tail -1; }
if powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WS\\ui.ps1" -Dump 2>&1 | grep -q "Sign In"; then
  P ui.ps1 -Set "Gate server address" -Value "${GC_SERVER:-127.0.0.1:8080}"
  P ui.ps1 -Set "Username" -Value "${GC_USER:-demo-admin}"
  P ui.ps1 -Set "Password" -Value "${GC_PASS:-demo-open-sesame}"
  P ui.ps1 -Invoke "Sign In"
  sleep 5
fi
