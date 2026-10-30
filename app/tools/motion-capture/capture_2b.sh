#!/bin/bash
# capture_2b.sh — Phase 2b evidence on the running app (Release + harness build):
# every tab entrance (first and later visit) traced in-app AND filmed, theme
# reveal latency bursts, Reduce Motion entrances.
source "$(dirname "$(readlink -f "$0")")/env.sh"
B=(1 2 3 4 0)
for pass in F L; do
  for t in "${B[@]}"; do
    P burst.ps1 -Out "$SP\\ent-$pass-$t" -Frames 45 -Actions "1:trace 1800 $pass-$t;2:tab $t" -Scale 0.5 >/dev/null
    sleep 1.5
  done
done
P send.ps1 -Cmd "tab 0" >/dev/null; sleep 2
P burst.ps1 -Out "$SP\\theme-dark" -Frames 40 -Actions "2:themepress;5:theme dark" -Scale 0.5 >/dev/null; sleep 2
P burst.ps1 -Out "$SP\\theme-light" -Frames 40 -Actions "2:themepress;5:theme light" -Scale 0.5 >/dev/null; sleep 2
P burst.ps1 -Out "$SP\\theme-dark-nopress" -Frames 40 -Actions "2:theme dark" -Scale 0.5 >/dev/null; sleep 2
P send.ps1 -Cmd "reduce on" >/dev/null; sleep 1
for t in 1 3; do
  P burst.ps1 -Out "$SP\\ent-RM-$t" -Frames 24 -Actions "1:trace 900 RM-$t;2:tab $t" -Scale 0.5 >/dev/null; sleep 1
done
P burst.ps1 -Out "$SP\\theme-RM" -Frames 24 -Actions "2:theme light" -Scale 0.5 >/dev/null; sleep 1
P send.ps1 -Cmd "reduce os" >/dev/null
echo done
