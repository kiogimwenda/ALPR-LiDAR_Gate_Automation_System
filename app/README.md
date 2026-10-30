# Gate Control — the operator app

A native app for **Android, iOS, macOS (Mac Catalyst) and Windows**,
built with .NET MAUI. Windows is built and run-tested; **Android: built, not
run-tested; needs a device**; iOS / macOS are not built here. It replaces the old web dashboard (see
[ADR-013](../docs/decisions/ADR-013-native-operator-app.md)) and talks to
`gate-dashboard`'s HTTP + WebSocket API.

## What it does

| Tab | For | What's on it |
|---|---|---|
| **Live** | guards | One tile per gate: state, safety beam, link, last vehicle. Open, Close, Hold open, Lock down, Release. The vehicle feed as plates are read, with haptics on blocklisted and held vehicles. |
| **Dashboard** | everyone | Today / 7 days / 30 days. Vehicles, allowed, refused, on site now, entries, exits, average stay, busiest hour, gates online, open alerts. Charts: vehicles by hour, entries vs exits, results, vehicle types, length of stay. Most frequent plates. Refreshes every 30 s. |
| **Reports** | everyone | **Search** across all decisions by plate (any part), result, vehicle type, direction, gate, minimum confidence and dates, with paging and **CSV export** via the share sheet. **Visits**: on site now, longest visits, dwell distribution. **Unreadable**: no-plate and low-confidence frames, by reason. **Trend**: vehicles and refusals per day. |
| **Vehicles** | operators | The registered (allowlisted) vehicles: search, add or edit in a sheet (owner, unit, notes, allowed vehicle types, expiry), swipe to remove. |
| **Alerts** | everyone (operators clear) | Blocklisted plates, vehicle-type mismatches (swapped plates), gate faults, failed commands. Clear one or all. Live badge count. |
| **Account** (person button) | everyone / admins | Profile, appearance (auto, light, dark), change password, sign out. Admins: **Users and roles**, **Gates** (name, location, entry / exit / both), **Data and retention**. |

Tapping any plate opens its history: visits, first and last seen, every
decision, whether it is registered.

Roles come from the server: **viewer** (reports), **operator** (plus
gates, vehicles, alerts), **admin** (plus users, gates, data). The app
hides what a role can't do; the server enforces it.

## Design

The interface follows Apple's motion and typography principles (the
"apple-design" brief, from WWDC). Where they live in code:

| Principle | Implementation |
|---|---|
| Respond on press | `Motion/Motion.cs` `Pressable`: scale 0.97 on touch-**down** (~100 ms spring). Every `Button` gets it through a handler mapping. |
| Interruptible | One long-lived spring per view and property; a new target retargets it, keeping position and velocity (`Spr.To`). |
| Springs, not curves | `GateControl.Core/Motion/Spring.cs`: damping ratio + response, closed-form step (exact at any frame rate). Two measured exceptions use a timed curve (`Motion/Timing.cs`): number count-ups (ease-out-cubic 1.45 s) and the theme reveal (ease-in-quad 0.62 s). |
| Direct manipulation, rubber-banding, velocity handoff, momentum | `Controls/Sheet.cs`: drag 1:1 from the grab point, 0.55 resistance past the top, release judged by projected landing (`Gesture.Project`, 0.998), flick speed carried into the spring. |
| Spatial consistency | Sheets leave the way they came (down). Feed rows drop in from above. |
| Gesture details | A press that moves ~10 pt becomes a scroll: the highlight cancels. |
| Smooth frames | Only transform and opacity are animated, plus three documented exceptions: count-up text, the wavy clip of the theme reveal / accent tile (`Motion/BlobClip.cs`), and the reveal blur on Android 12+ (`Motion/PlatformBlur.cs`, off by default until measured on a device). On Windows nothing creates WinUI objects per frame (`Platform/WinTransform.cs`, the native blob clip, cached chart text formats), and WinUI's own memory-check collection is run at idle after the tabs are warmed (`RootPage.SettleXamlCleanup`); measured: every tab entrance 0 dropped frames over 5 launches, tap → pill motion ≤ 9 ms; the light/dark wipe starts ≤ 91 ms after the tap with 0 dropped frames (Windows, 240 Hz, Release build with the measurement harness; `docs/phase2b-evidence/round2/acceptance-table.md`, `theme-reveal.md`). Parked tabs are kept out of keyboard focus and UI Automation (`Controls/ParkHost.cs`). Known: the frame ticker's one CompositionTarget.Rendering handler gets a WinRT event-args wrapper per frame, whose memory pressure can occasionally induce a background gen-2 collection while something animates (one 39 ms frame seen in a 40 s traced run; none in the 45 measured entrances). |
| Materials | One translucent layer (tab bar chrome, sheet scrim); never glass on glass. |
| Multimodal feedback | `Platform/PlatformServices.cs` `Haptics`: success, warning and error notifications on iOS, confirm and reject on Android, in the same frame as the motion. |
| Reduced motion | `MotionSettings.ReduceMotion` reads the OS setting on every platform; slides become quick cross-fades, press scale becomes a dim. |
| Typography | `Resources/Styles/Styles.xaml`: Inter ("Inter GC", tabular digits frozen in so counting numbers never jitter); tracking tightens as type grows. |

The visual design (layout, palette, motion catalogue) is specified in
[docs/design-video-spec.md](docs/design-video-spec.md). The shell is one
persistent page (`Pages/RootPage.cs`): a floating header with the
navigation capsule and its sliding pill, a left rail on wide windows, and
the same capsule at the bottom on phones; the light/dark toggle is at the
top-right end of the header. Colours are semantic
(`Resources/Styles/Colors.xaml`, light and dark): ink for selection and
primary actions, one brand orange used sparingly, and four meanings
(positive, negative, caution, neutral). Views ask for a meaning, never a
hue.

## Projects

```
app/
├── GateControl.slnx                    open this in Visual Studio
├── src/GateControl.Core/               API + live feed, models, view models, motion physics (net10.0)
├── src/GateControl.App/                MAUI app: pages, motion engine, sheet, charts, platform services
└── tests/GateControl.Core.Tests/       xUnit
```

## Build and run

Requirements: .NET 10 SDK and the MAUI workloads (`dotnet workload
install maui`); Visual Studio 2026 with ".NET Multi-platform App UI".

### Opening it from Windows (the repo lives in WSL)

There is one copy of the app: `app/` in this repo. Visual Studio reaches
it through a drive letter mapped to the WSL distro, since the Windows App
SDK tools cannot build from `\\wsl.localhost\...` UNC paths:

```powershell
net use W: \\wsl.localhost\Debian /persistent:yes   # once; undo with: net use W: /delete
```

Then in Visual Studio: **File → Open → Project/Solution →
`W:\home\deby\projects\gate-automation\app\GateControl.slnx`**, set
**GateControl.App** as the startup project, pick **Windows Machine** or an
**Android Emulator**, and press F5.

On Windows, `bin/` and `obj/` go to `%LOCALAPPDATA%\GateControl\build`
(Directory.Build.props), not beside the sources. WinUI won't start an app
from a network drive, and build output across the WSL boundary is slow.

### Command line

```powershell
dotnet test  W:\home\deby\projects\gate-automation\app\tests\GateControl.Core.Tests
dotnet build W:\home\deby\projects\gate-automation\app\src\GateControl.App -f net10.0-windows10.0.19041.0
dotnet build W:\home\deby\projects\gate-automation\app\src\GateControl.App -f net10.0-android -t:Run
# iOS / macOS: on a Mac with Xcode, or via Visual Studio's Tools → iOS → Pair to Mac
```

### A server to talk to

In WSL, run `scripts/demo-stack.sh --hardened` (it prints demo
credentials) or `./run_mock_stack.sh` (open access). Then sign in with
server **`127.0.0.1:8080`**. From Windows, use `127.0.0.1` rather than
`localhost`, which resolves to IPv6 first and stalls on the WSL relay.
From the Android emulator, use **`10.0.2.2:8080`**.

## Server requirements

gate-dashboard with its history store (`--history-db`, on by default).
The app uses:
- `/api/auth/*`, `/api/status`, `/ws/events` and `/api/gates/*/command`;
- `/api/reports/*`, `/api/decisions` (search + `/export` CSV) and
  `/api/plates/{plate}`;
- `/api/alerts`, `/api/gates`, `/api/users`, `/api/settings` and
  `/api/allowlist`.

Plain HTTP on the site LAN is permitted by the app's network policy.
Put the dashboard behind TLS for anything beyond the site LAN.

## Third-party notices

| Component | Where | Licence |
|---|---|---|
| [Inter](https://github.com/rsms/inter) 4.1, Copyright 2016 The Inter Project Authors | `src/GateControl.App/Resources/Fonts/InterGC-*.ttf` | SIL Open Font License 1.1, full text in [`Resources/Fonts/OFL.txt`](src/GateControl.App/Resources/Fonts/OFL.txt). Modified version: the `tnum` feature is frozen into the character map for the digits 0–9 only (tabular figures by default; punctuation unchanged) and the family is renamed **"Inter GC"**, as the OFL asks of modified fonts. Rebuild with `tools/design-assets/freeze_tnum.py`. |
| [Lucide](https://lucide.dev) icons 0.460.0 (portions from Feather, MIT) | sources `src/GateControl.App/Resources/Icons/*.svg`; generated path data `Controls/Icons.cs` | ISC licence, full text in [`Resources/Icons/LICENSE.txt`](src/GateControl.App/Resources/Icons/LICENSE.txt). Regenerate `Icons.cs` with `tools/design-assets/lucide2cs.py`. |
