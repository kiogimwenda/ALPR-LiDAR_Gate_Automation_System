#!/bin/bash
# a11y_check.sh <out dir> — with each tab active in turn: every keyboard-reachable control (harness
# "tabstops": IsTabStop, enabled, visible), split by whether it lies in a parked tab, and a UI
# Automation control-view dump (ui.ps1 -Dump); then counts
# dump entries that lie outside the window (parked content sits 20000 dip to the right).
source "$(dirname "$(readlink -f "$0")")/env.sh"
OUT=$1; mkdir -p "$OUT"; C=/mnt/c/Users/User/AppData/Local/Temp/gc-cmd.txt; L=$C.log
WS=$(wslpath -w "$T")
ensure_demo
P launch.ps1 -Width 1500 -Height 960 -Config Release >/dev/null; sleep 10
P send.ps1 -Cmd "signout" >/dev/null; sleep 3; timeout 120 "$T/signin.sh" >/dev/null; sleep 16
: > $L
for t in 0 1 2 3 4; do
  printf "tab $t\n" > $C; sleep 3
  printf "tabstops\n" > $C; sleep 3
  timeout 90 powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WS\\ui.ps1" -Dump > "$OUT/uia-tab$t.txt" 2>&1
  W=$(powershell.exe -NoProfile -Command "Add-Type -AssemblyName UIAutomationClient; \$h=(Get-Process GateControl).MainWindowHandle; [System.Windows.Automation.AutomationElement]::FromHandle(\$h).Current.BoundingRectangle.Right" | tr -d '\r')
  echo "tab $t: $(wc -l < "$OUT/uia-tab$t.txt") UIA elements, $(awk -F'|' -v w="$W" '{split($4,a,","); if (a[1]+0 > w+0) n++} END {print n+0}' "$OUT/uia-tab$t.txt") outside the window (right edge $W)"
done
grep -E "tabstops" $L
