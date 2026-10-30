#!/bin/bash
# theme_bursts.sh <name> [WxH] — fresh launch, Dashboard, then a filmed wipe each way
# (pointer-down + tap ~2 burst frames apart). Bursts: %TEMP%/gcburst/<name>-{light,dark}.
source "$(dirname "$(readlink -f "$0")")/env.sh"
SIZE=${2:-1500x960}; C=/mnt/c/Users/User/AppData/Local/Temp/gc-cmd.txt
ensure_demo
P launch.ps1 -Width ${SIZE%x*} -Height ${SIZE#*x} -Config Release >/dev/null; sleep 10
P send.ps1 -Cmd "signout" >/dev/null; sleep 3; timeout 120 "$T/signin.sh" >/dev/null; sleep 16
printf "tab 1\n" > $C; sleep 3; printf "theme dark\n" > $C; sleep 2
SP=$(wslpath -w /mnt/c/Users/User/AppData/Local/Temp)\\gcburst
P burst.ps1 -Out "$SP\\$1-light" -Frames 36 -Actions "3:themepress;5:theme light" -Scale 0.5 >/dev/null; sleep 2
P burst.ps1 -Out "$SP\\$1-dark" -Frames 36 -Actions "3:themepress;5:theme dark" -Scale 0.5 >/dev/null
