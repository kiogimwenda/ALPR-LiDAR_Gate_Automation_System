#!/bin/bash
# theme_runs.sh [runs] [WxH] [prefix] — the light/dark wipe, traced in-app: a fresh launch, then
# per run dark→light and light→dark, each as pointer-down (themepress) + tap (theme) ~90 ms apart,
# like a click on the top-right toggle. Traces: %TEMP%/gc-<prefix><run>-{L,D}-*.csv.
source "$(dirname "$(readlink -f "$0")")/env.sh"
RUNS=${1:-5}; SIZE=${2:-1500x960}; PRE=${3:-TH}
C=/mnt/c/Users/User/AppData/Local/Temp/gc-cmd.txt
ensure_demo
P launch.ps1 -Width ${SIZE%x*} -Height ${SIZE#*x} -Config Release >/dev/null; sleep 10
P send.ps1 -Cmd "signout" >/dev/null; sleep 3; timeout 120 "$T/signin.sh" >/dev/null; sleep 16
printf "theme dark\n" > $C; sleep 2
for r in $(seq 1 $RUNS); do
  for to in light dark; do
    printf "trace 1400 $PRE$r-${to:0:1}\nthemepress\n" > $C; sleep 0.09; printf "theme $to\n" > $C; sleep 2.5
  done
done
echo done
