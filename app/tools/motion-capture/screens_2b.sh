#!/bin/bash
# screens_2b.sh — every page in light and dark, wide and phone.
source "$(dirname "$(readlink -f "$0")")/env.sh"
shoot(){ P send.ps1 -Cmd "$1" >/dev/null; sleep ${3:-2.5}; P shot.ps1 -Out "$SP\\$2.png" >/dev/null; }
for theme in light dark; do
  P send.ps1 -Cmd "theme $theme" >/dev/null; sleep 2
  shoot "tab 0" "w-$theme-live"; shoot "tab 1" "w-$theme-dashboard"; shoot "tab 2" "w-$theme-reports"
  shoot "tab 3" "w-$theme-vehicles"; shoot "tab 4" "w-$theme-alerts"
  shoot "plate KDA123X" "w-$theme-alerts-pane" 3
  shoot "tab 2" "w-$theme-reports-x"; shoot "plate KDA123X" "w-$theme-reports-pane" 3
  P send.ps1 -Cmd "poproot" >/dev/null; sleep 1
  shoot "push settings" "w-$theme-settings"; P send.ps1 -Cmd "pop" >/dev/null; sleep 1
  shoot "push users" "w-$theme-users"; P send.ps1 -Cmd "pop" >/dev/null; sleep 1
  shoot "push gates" "w-$theme-gates"; P send.ps1 -Cmd "pop" >/dev/null; sleep 1
  shoot "push data" "w-$theme-data"; P send.ps1 -Cmd "pop" >/dev/null; sleep 1
  shoot "tab 0" "x"; shoot "plate KDA123X" "w-$theme-plate" 3; P send.ps1 -Cmd "pop" >/dev/null; sleep 1
done
P send.ps1 -Cmd "size 420 860" >/dev/null; sleep 3
for theme in light dark; do
  P send.ps1 -Cmd "theme $theme" >/dev/null; sleep 2
  shoot "tab 0" "p-$theme-live"; shoot "tab 1" "p-$theme-dashboard"; shoot "tab 3" "p-$theme-vehicles"; shoot "tab 4" "p-$theme-alerts"
done
P send.ps1 -Cmd "size 1000 640" >/dev/null; P send.ps1 -Cmd "theme light" >/dev/null; P send.ps1 -Cmd "tab 0" >/dev/null
echo done
