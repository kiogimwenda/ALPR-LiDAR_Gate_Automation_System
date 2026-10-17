#!/bin/bash
# route.sh <tag> [passes] : fanout -> DSN -> Freerouting -> import -> DRC   (input: route/placed.kicad_pcb)
set -e
S=$(cd "$(dirname "$0")" && pwd); T=$1; MP=${2:-80}; R=$S/route; O=$S/$T
PY="/mnt/c/Program Files/KiCad/10.0/bin/python.exe"; K="/mnt/c/Program Files/KiCad/10.0/bin/kicad-cli.exe"
J="/mnt/c/Program Files/Eclipse Adoptium/jdk-25.0.2.10-hotspot/bin/java.exe"
w() { wslpath -w "$1"; }
mkdir -p "$O"; cp "$R"/*.kicad_sch "$R/gate-field-controller.kicad_pro" "$O/"
# KiCad reads rules/net classes from <board-stem>.kicad_pro: give every stage its copy
PRO="$R/gate-field-controller.kicad_pro"; cp "$PRO" "$R/placed.kicad_pro"; cp "$PRO" "$O/fan.kicad_pro"
cp "$PRO" "$O/prep.kicad_pro"
"$PY" "$(w $S/prep.py)" "$(w $R/placed.kicad_pcb)" "$(w $O/prep.kicad_pcb)" 2>&1 | grep -v leak
"$PY" "$(w $S/fanout.py)" "$(w $O/prep.kicad_pcb)" "$(w $O/fan.kicad_pcb)" 2>&1 | grep -v leak
"$PY" "$(w $S/dsn.py)" "$(w $O/fan.kicad_pcb)" "$(w $O)\\fan.dsn" 2>&1 | grep -v leak >/dev/null
python3 - "$O/fan.dsn" <<'PYEOF'
import sys
p = sys.argv[1]; s = open(p).read()
for L in ("In1.Cu", "In2.Cu"):
    a = f"(layer {L}\n      (type signal)"; assert a in s; s = s.replace(a, f"(layer {L}\n      (type power)")
open(p, "w").write(s)
PYEOF
"$J" -jar "$(w $S/fr/freerouting-2.5.0.jar)" -de "$(w $O/fan.dsn)" -do "$(w $O/fan.ses)" -mp $MP --router.automatic_neckdown=false --gui.enabled=false > "$O/fr.log" 2>&1
grep -E "Auto-routing stage completed|Optimization stage completed|Skipping optim" "$O/fr.log" | sed 's/.*INFO *//' | cut -c1-220
"$PY" "$(w $S/post.py)" "$(w $O/fan.kicad_pcb)" "$(w $O/fan.ses)" "$(w $O)\\gate-field-controller.kicad_pcb" 2>&1 | grep -v leak
cp "$PRO" "$O/gate-field-controller.kicad_pro"; rm -f "$O"/*.kicad_prl
"$K" pcb drc --refill-zones --schematic-parity --severity-all -o "$(w $O)\\drc.rpt" "$(w $O)\\gate-field-controller.kicad_pcb" 2>&1 | grep Found
grep -E "^\[" "$O/drc.rpt" | sed 's/(.*//' | sort | uniq -c | sort -rn
