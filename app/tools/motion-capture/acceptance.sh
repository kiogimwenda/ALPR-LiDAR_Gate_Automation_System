#!/bin/bash
# acceptance.sh [runs] — the review's acceptance: N fresh launches; each traces the first visit of
# tabs 1–4, a later visit of every tab, and two Reduce Motion entrances. Traces: %TEMP%/gc-A<run>-*.
# Each run ends signed out (the next one signs in fresh: a restored session's token can expire
# mid-run and drop the app to the sign-in page). A run whose traces are not real entrances
# (acceptance.py --check) is repeated, at most twice.
source "$(dirname "$(readlink -f "$0")")/env.sh"
RUNS=${1:-5}
for r in $(seq 1 $RUNS); do for attempt in 1 2 3; do
  ensure_demo
  P launch.ps1 -Width 1500 -Height 960 -Config Release >/dev/null; sleep 10
  P send.ps1 -Cmd "signout" >/dev/null; sleep 3   # never trace on a restored (possibly stale) session
  timeout 120 "$T/signin.sh" >/dev/null; sleep 16
  for pass in F L; do for t in 1 2 3 4 0; do
    [ $pass = F ] && [ $t = 0 ] && continue
    printf "trace 1800 A$r-$pass-$t\ntab $t\n" > /mnt/c/Users/User/AppData/Local/Temp/gc-cmd.txt; sleep 3
  done; done
  P send.ps1 -Cmd "reduce on" >/dev/null; sleep 1
  for t in 1 3; do printf "trace 900 A$r-RM-$t\ntab $t\n" > /mnt/c/Users/User/AppData/Local/Temp/gc-cmd.txt; sleep 2; done
  P send.ps1 -Cmd "reduce os" >/dev/null; sleep 1
  P send.ps1 -Cmd "signout" >/dev/null; sleep 3
  if python3 -I "$T/acceptance.py" --check $r; then echo "run $r done"; break; fi
  echo "run $r invalid (attempt $attempt), repeating"
done; done
