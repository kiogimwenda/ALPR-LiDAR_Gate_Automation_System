#!/bin/bash
# round2.sh — review round 2 evidence (screens + functional checks), saved straight into the repo.
source "$(dirname "$(readlink -f "$0")")/env.sh"
E=/home/deby/projects/gate-automation/app/docs/phase2b-evidence/round2
EW=$(wslpath -w "$E")
D(){ (cd /mnt/c && powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WS\\ui.ps1" -Dump) | tr -d '\r'; }
shot(){ P shot.ps1 -Out "$EW\\$1.png" >/dev/null; }
ensure_demo
P launch.ps1 -Width 1500 -Height 1450 -Config Release >/dev/null; sleep 10; "$T/signin.sh" >/dev/null; sleep 8
P send.ps1 -Cmd "theme light" >/dev/null; sleep 2
# gate latch commands (simulator refuses them): status + error haptic
P send.ps1 -Cmd "tab 0" >/dev/null; sleep 2
P send.ps1 -Cmd "gate gate-demo-02 hold" >/dev/null; sleep 2; P ui.ps1 -Invoke "Hold open" >/dev/null; sleep 4; shot gate-hold
P send.ps1 -Cmd "gate gate-demo-02 lockdown" >/dev/null; sleep 2; P ui.ps1 -Invoke "Lock down" >/dev/null; sleep 4; shot gate-lockdown
P send.ps1 -Cmd "gate gate-demo-02 release" >/dev/null; sleep 4; shot gate-release
# alerts: clear one
P send.ps1 -Cmd "tab 4" >/dev/null; sleep 3
echo "alerts before: $(D | grep -oE '[0-9]+ open alerts' | head -1)" > "$E/functional.txt"; shot alerts-before-clear-one
idx=$(D | grep -E 'Button \| Clear \|' | grep -vn 'Empty' | head -1 | cut -d: -f1)
P ui.ps1 -Invoke "Clear" -Index $(( $(D | grep -E 'Button \| Clear \|' | grep -n -v Empty | head -1 | cut -d: -f1) - 1 )) >/dev/null; sleep 4
echo "alerts after clear one: $(D | grep -oE '[0-9]+ open alerts' | head -1)" >> "$E/functional.txt"; shot alerts-after-clear-one
# new user defaults to Viewer
P send.ps1 -Cmd "push users" >/dev/null; sleep 3; P ui.ps1 -Invoke "+  Add user" >/dev/null; sleep 2; shot user-new-default-viewer
P ui.ps1 -Invoke "Cancel" >/dev/null; sleep 2; P send.ps1 -Cmd "poproot" >/dev/null; sleep 1
# settled screens, every tab, light and dark (ghost check)
for theme in light dark; do
  P send.ps1 -Cmd "theme $theme" >/dev/null; sleep 2
  for t in "0 live" "1 dashboard" "2 reports" "3 vehicles" "4 alerts"; do set -- $t; P send.ps1 -Cmd "tab $1" >/dev/null; sleep 4; shot "settled-$theme-$2"; done
done
P send.ps1 -Cmd "theme light" >/dev/null; sleep 1
P send.ps1 -Cmd "size 420 900" >/dev/null; sleep 3
for t in "0 live" "1 dashboard" "4 alerts"; do set -- $t; P send.ps1 -Cmd "tab $1" >/dev/null; sleep 4; shot "phone-$2"; done
P send.ps1 -Cmd "size 1000 966" >/dev/null; sleep 2
grep -i "haptic" "/mnt/c/Users/User/AppData/Local/User Name/com.gateautomation.gatecontrol/Data/gatecontrol.log" | tail -8 >> "$E/functional.txt"
echo done
