#!/bin/bash
# commit_groups_stage_build.sh [worktree dir] [groups 1–6 snapshot] — replay the commit groups, in
# order, onto a
# detached worktree of HEAD and build after each one (Windows Debug, with its own build root so
# the working tree's outputs are untouched); Core tests after groups 2, 5 and 7; Android Debug at
# the end. Groups 1–6 are copied from a snapshot of app/ taken when they were approved (second
# argument; default: this working tree); group 7 (theme press fixes + keyboard accessibility) is
# every file that differs between that snapshot and this working tree.
# Deletions are listed with a leading "-".
# Never commits. Prints one line per group.
set -u
REPO=$(git -C "$(dirname "$(readlink -f "$0")")" rev-parse --show-toplevel)
WT=${1:-/tmp/gc-stage-worktree}
SNAP=${2:-$REPO}
DOTNET="/mnt/c/Program Files/dotnet/dotnet.exe"
STAGE_ROOT='C:\Users\User\AppData\Local\GateControl\stage\'
A=src/GateControl.App; C=src/GateControl.Core; T=tests/GateControl.Core.Tests
G1="$A/Resources/Styles/Colors.xaml $A/Resources/Styles/Styles.xaml $A/Resources/Fonts $A/Resources/Icons $A/Resources/Images/accent_glow.png $A/Resources/Images/brand_gate.svg $A/Resources/AppIcon/appicon.svg $A/Resources/Splash/splash.svg tools/design-assets"
G2="$C/Motion/Spring.cs $C/Motion/Timing.cs $C/Formatting/NumberText.cs $C/Formatting/TileMath.cs $T/NumberTextTests.cs $T/TimingTests.cs"
G3="$A/Motion $A/Controls $A/Platform/AppLog.cs $A/Platform/WinTransform.cs"
G4="$A/Pages/RootPage.cs $A/Pages/TwoPane.cs $A/Pages/IPlatePaneHost.cs $A/Pages/PageBase.cs $A/Platform/PlatformServices.cs $A/Platform/Automation.cs -$A/AppShell.cs $A/App.xaml.cs $A/MauiProgram.cs $A/GateControl.App.csproj $A/Platforms/Windows/App.xaml $A/Platforms/Windows/App.xaml.cs"
G5="$A/Pages $A/Views $C/ViewModels $T/ViewModelTests.cs"
G6="README.md docs tools"
build() {  # $1 = label
  local w; w=$(wslpath -w "$WT/app/src/GateControl.App")
  (cd /mnt/c && timeout 1800 "$DOTNET" build "$w" -f net10.0-windows10.0.19041.0 -c Debug -v q -nologo -p:GateBuildRoot="$STAGE_ROOT" 2>&1) > "$WT/../stage-$1.log"
  local rc=$?; echo "$1 windows: exit $rc, $(grep -E 'Warning\(s\)|Error\(s\)' "$WT/../stage-$1.log" | tr -s ' ' | tr '\n' ' ')"
}
tests() {
  local w; w=$(wslpath -w "$WT/app/tests/GateControl.Core.Tests")
  (cd /mnt/c && timeout 900 "$DOTNET" test "$w" -v q -nologo -p:GateBuildRoot="$STAGE_ROOT" 2>&1) | grep -E "Passed!|Failed!|error|^\s+Failed " | head -8 | sed "s/^/$1 tests: /"
}
git -C "$REPO" worktree add --detach "$WT" HEAD >/dev/null 2>&1 || { echo "worktree add failed (exists?)"; exit 1; }
n=0
for G in "$G1" "$G2" "$G3" "$G4" "$G5" "$G6"; do
  n=$((n+1))
  for p in $G; do
    if [ "${p:0:1}" = "-" ]; then git -C "$WT" rm -q -r "app/${p:1}" 2>/dev/null || rm -rf "$WT/app/${p:1}"
    else mkdir -p "$(dirname "$WT/app/$p")"; cp -r "$SNAP/app/$p" "$(dirname "$WT/app/$p")/"; fi
  done
  # never the per-user files, nor the large evidence bursts (not committed)
  find "${WT:?}/app" -name '*.csproj.user' -delete
  rm -rf "${WT:?}/app/docs/phase2b-evidence/smoothness" "${WT:?}/app/docs/phase2b-evidence/theme"
  build "group$n"
  { [ $n = 2 ] || [ $n = 5 ]; } && tests "group$n"
done
# group 7: everything that changed after the snapshot
if [ "$SNAP" != "$REPO" ]; then
  G7=$(cd "$REPO" && diff -rq "$SNAP/app" app -x obj -x bin -x .idea -x '*.user' -x smoothness -x theme 2>/dev/null |
       sed -E "s#^Files $SNAP/app/([^ ]+) and .*#\1#; s#^Only in app/?([^:]*): (.*)#\1/\2#; s#^/##" | grep -v "^Only in" | sort)
  echo "group7 files:"; echo "$G7" | sed 's/^/  /'
  for p in $G7; do mkdir -p "$(dirname "$WT/app/$p")"; cp -r "$REPO/app/$p" "$(dirname "$WT/app/$p")/"; done
  find "${WT:?}/app" -name '*.csproj.user' -delete
  build group7
  tests group7
fi
w=$(wslpath -w "$WT/app/src/GateControl.App")
(cd /mnt/c && timeout 2400 "$DOTNET" build "$w" -f net10.0-android -c Debug -v q -nologo -p:GateBuildRoot="$STAGE_ROOT" 2>&1) > "$WT/../stage-android.log"
echo "final android: exit $?, $(grep -E 'Warning\(s\)|Error\(s\)' "$WT/../stage-android.log" | tr -s ' ' | tr '\n' ' ')"
echo "diff vs working tree (app/, excluding evidence bursts):"
diff -rq "$REPO/app" "$WT/app" -x smoothness -x theme -x '*.user' 2>/dev/null | head
