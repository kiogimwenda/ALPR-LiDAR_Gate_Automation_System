#!/bin/bash
# settle_check.sh — review round 3 item 5: after a resize, a maximise / restore, and a theme
# change, enter a tab (traced) and look for gen-2 collections / long frames in the entrance;
# also a tab switch during the 120-frame settle nudge (interrupted run).
source "$(dirname "$(readlink -f "$0")")/env.sh"
C=/mnt/c/Users/User/AppData/Local/Temp/gc-cmd.txt
ensure_demo
P launch.ps1 -Width 1500 -Height 960 -Config Release >/dev/null; sleep 10
P send.ps1 -Cmd "signout" >/dev/null; sleep 3; timeout 120 "$T/signin.sh" >/dev/null; sleep 16
t=1
for r in 1 2 3; do
  printf "size 1200 780\n" > $C; sleep 4; printf "trace 1800 SR$r-resize\ntab $t\n" > $C; sleep 3; t=$(( t % 4 + 1 ))
  printf "size 1000 640\n" > $C; sleep 3
  P winmax.ps1 -State max >/dev/null; sleep 3; P winmax.ps1 -State restore >/dev/null; sleep 4
  printf "trace 1800 SR$r-maxrestore\ntab $t\n" > $C; sleep 3; t=$(( t % 4 + 1 ))
  printf "themepress\n" > $C; sleep 0.09; printf "theme light\n" > $C; sleep 4; printf "theme dark\n" > $C; sleep 4
  printf "trace 1800 SR$r-theme\ntab $t\n" > $C; sleep 3; t=$(( t % 4 + 1 ))
done
# interrupted settle: resize, then switch tab 0.75 s later (inside the 120-frame nudge)
printf "size 1200 780\n" > $C; sleep 0.75; printf "trace 1800 SR-interrupt\ntab 2\n" > $C; sleep 3
printf "size 1000 640\n" > $C; sleep 2
cd /mnt/c/Users/User/AppData/Local/Temp
python3 -I - <<'PY'
import csv, glob, os
for f in sorted(glob.glob('gc-SR*-frames.csv')):
    p = os.path.basename(f)[3:-11]
    fr = list(csv.DictReader(open(f))); t0 = float(fr[0]['ms'])
    ev = list(csv.DictReader(open(f'gc-{p}-events.csv')))
    lf = [(round(float(b['ms']) - t0), round(float(b['ms']) - float(a['ms']))) for a, b in zip(fr, fr[1:]) if float(b['ms']) - float(a['ms']) > 25]
    settle = sum(1 for e in ev if e['event'] == 'prewarm.settle')
    print(f"{p}: gen2 {fr[0]['gc2']}->{fr[-1]['gc2']} gen0 {fr[0]['gc0']}->{fr[-1]['gc0']} frames>25ms {lf} settle-starts-in-trace {settle}")
PY
