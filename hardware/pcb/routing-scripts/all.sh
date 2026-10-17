#!/bin/bash
# all.sh <tag> : route.sh <tag> then finish.py -> fin<tag>/ and DRC
S=$(cd "$(dirname "$0")" && pwd); T=$1
"$S/route.sh" "$T" 100 2>&1 | grep -v "^$\|image handler"
O=$S/$T; F=$S/fin-$T; mkdir -p "$F"; cp "$O"/*.kicad_sch "$F/"
PY="/mnt/c/Program Files/KiCad/10.0/bin/python.exe"; K="/mnt/c/Program Files/KiCad/10.0/bin/kicad-cli.exe"; w(){ wslpath -w "$1"; }
cp "$S/route/gate-field-controller.kicad_pro" "$O/"
"$PY" "$(w $S/finish.py)" "$(w $O/gate-field-controller.kicad_pcb)" "$(w $F)\\gate-field-controller.kicad_pcb" 2>&1 | grep -v "leak\|image handler"
cp "$S/route/gate-field-controller.kicad_pro" "$F/"; rm -f "${F:?}"/*.kicad_prl
"$K" pcb drc --refill-zones --schematic-parity --severity-all -o "$(w $F)\\drc.rpt" "$(w $F)\\gate-field-controller.kicad_pcb" 2>&1 | grep Found
grep -E "^\[" "$F/drc.rpt" | sed 's/(.*//' | sort | uniq -c
