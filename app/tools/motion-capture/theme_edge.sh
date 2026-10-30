#!/bin/bash
# theme_edge.sh — the wipe's edge cases, in-app: tap again mid-wipe, switch tab mid-wipe, resize
# mid-wipe, press held > 1 s, and Reduce Motion; after each, "themeinfo" logs the theme, whether
# a press/wipe is still in progress, leftover overlays and the caption colour.
source "$(dirname "$(readlink -f "$0")")/env.sh"
C=/mnt/c/Users/User/AppData/Local/Temp/gc-cmd.txt; L=$C.log
ensure_demo
P launch.ps1 -Width 1500 -Height 960 -Config Release >/dev/null; sleep 10
P send.ps1 -Cmd "signout" >/dev/null; sleep 3; timeout 120 "$T/signin.sh" >/dev/null; sleep 16
printf "tab 1\n" > $C; sleep 2; printf "theme dark\n" > $C; sleep 2
: > $L
step(){ echo "== $1" >> $L; }
step "retap mid-wipe (to light, then to dark 250 ms in)"
printf "trace 1600 TE-retap\nthemepress\n" > $C; sleep 0.09; printf "theme light\n" > $C; sleep 0.25; printf "themepress\n" > $C; sleep 0.09; printf "theme dark\n" > $C; sleep 2.5; printf "themeinfo\n" > $C; sleep 1
step "tab switch mid-wipe (to light, Reports 250 ms in)"
printf "trace 1600 TE-tab\nthemepress\n" > $C; sleep 0.09; printf "theme light\n" > $C; sleep 0.25; printf "tab 2\n" > $C; sleep 2.5; printf "themeinfo\n" > $C; sleep 1
step "resize mid-wipe (to dark, 1100x800 250 ms in)"
printf "trace 1600 TE-size\nthemepress\n" > $C; sleep 0.09; printf "theme dark\n" > $C; sleep 0.25; printf "size 1100 800\n" > $C; sleep 2.5; printf "themeinfo\n" > $C; sleep 1
printf "size 1000 640\n" > $C; sleep 2
step "press held 1.4 s, then tap (to light)"
printf "trace 2400 TE-hold\nthemepress\n" > $C; sleep 1.4; printf "theme light\n" > $C; sleep 2.5; printf "themeinfo\n" > $C; sleep 1
step "press without a tap (cancelled press)"
printf "trace 1800 TE-cancel\nthemepress\n" > $C; sleep 2.5; printf "themeinfo\n" > $C; sleep 1
step "Reduce Motion (to dark)"
printf "reduce on\n" > $C; sleep 1; printf "trace 900 TE-rm\nthemepress\n" > $C; sleep 0.09; printf "theme dark\n" > $C; sleep 1.5; printf "themeinfo\n" > $C; sleep 1
printf "reduce os\n" > $C; sleep 1
grep -E "^==|themeinfo requested|error" $L
