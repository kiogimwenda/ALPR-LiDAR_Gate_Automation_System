#!/bin/bash
# trace_gc.sh <name> — fresh launch (Release + harness), sign in, then an EventPipe GC trace
# (dotnet-trace global tool, Windows) across the first tab entrance after launch (Dashboard),
# plus the in-app frame trace; prints long frames and every GC trigger with its stack.
# Output: %TEMP%\gctrace\<name>.nettrace / .txt, %TEMP%\gc-G-<name>-*.csv.
source "$(dirname "$(readlink -f "$0")")/env.sh"; ensure_demo
TMP=/mnt/c/Users/User/AppData/Local/Temp; mkdir -p $TMP/gctrace
P launch.ps1 -Width 1500 -Height 960 -Config Release >/dev/null; sleep 10; timeout 120 "$T/signin.sh" >/dev/null; sleep 14
PID=$(powershell.exe -NoProfile -Command "(Get-Process GateControl).Id" | tr -d '\r')
cd /mnt/c; ( /mnt/c/Users/User/.dotnet/tools/dotnet-trace.exe collect -p $PID --providers "Microsoft-Windows-DotNETRuntime:0x1:5" --duration 00:00:00:08 -o "C:\\Users\\User\\AppData\\Local\\Temp\\gctrace\\$1.nettrace" > /dev/null 2>&1 & )
sleep 3; printf "trace 1800 G-$1\ntab 1\n" > $TMP/gc-cmd.txt; sleep 10
python3 -I - "$TMP/gc-G-$1-frames.csv" <<'PY'
import csv, sys
fr = list(csv.DictReader(open(sys.argv[1]))); t0 = float(fr[0]['ms'])
print('gc count', fr[0]['gc0'], '->', fr[-1]['gc0'], 'long frames (ms in, ms)',
      [(round(float(b['ms']) - t0), round(float(b['ms']) - float(a['ms']))) for a, b in zip(fr, fr[1:]) if float(b['ms']) - float(a['ms']) > 20])
PY
"/mnt/c/Program Files/dotnet/dotnet.exe" run --project "$(wslpath -w "$T/trace-parser")" -c Release -- "C:\\Users\\User\\AppData\\Local\\Temp\\gctrace\\$1.nettrace" > $TMP/gctrace/$1.txt 2>&1
grep -E "GCTriggered|GCStart|global:" $TMP/gctrace/$1.txt
