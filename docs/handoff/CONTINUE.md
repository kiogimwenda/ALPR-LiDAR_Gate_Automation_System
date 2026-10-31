# Handoff prompt: continue the Gate Automation work

> Paste everything below the line into the next AI coding agent (Antigravity,
> Cursor, another Claude session, etc.). It is kept current at each milestone.
> ## NEXT PROGRAMME (agreed with the user 2026-10-30). Run in this order, each phase reviewed.
>
> **User decisions:**
> - **Operators:** commercial gate operators (option A). Candidate: Centurion
>   (D5/D10 Evo sliding, Vector2 swing); research and recommend.
> - **Gate types:** sliding, single-leaf and dual-leaf. A gate is
>   admin-configurable only when fully closed. A supervised short pulse
>   verifies the type (LiDAR/camera motion signature, operator status and
>   motor current) before auto-calibration. On a mismatch: stop, close,
>   alert.
> - **Lock (fail-secure):**
>   - Dual leaf: 2 solenoid bolts (top and bottom, leaf to leaf) + 3 maglocks
>     between them.
>   - Sliding and single leaf: 2 bolts (gate to pillar) + 2 maglocks.
>   - With power, maglocks and bolts are both active; without power, the
>     bolts alone hold.
>   - Each bolt and maglock has a position/bond feedback input. The gate
>     refuses to drive while any bolt is still extended.
> - **Exit:** a request-to-exit button and the app's Open become ONE open
>   request path with a source tag, sharing arbitration and audit (no
>   duplicate logic). If the server or network is down, the exit button
>   opens on the controller's own authority and the event is logged and
>   sent later. Entry stays locked without the server.
> - **Power loss:** the controller and locks run on battery/UPS by default.
> - **Sensing:** two camera + LiDAR pairs per gate, one facing entry and one
>   facing exit.
> - **Lane:** one shared lane unless configured otherwise. When both sides
>   wait, EXIT goes first and entry is queued; both are logged.
> - **Motor current:** a non-contact Hall or clamp-on sensor on the
>   operator motor lead, read by the board.
> - **Board rev 3:**
>   - 140×120 mm, as compact as possible, 4-layer, ESP32-S3 kept;
>   - the enclosure adjusted to match;
>   - a 240 V AC input through a certified encapsulated AC-DC module
>     (Mean Well IRM/LRS class) giving isolated 24 V and 12 V for the
>     whole gate side, the board included;
>   - 5 lock drivers plus lock-feedback inputs, the RTE button, the
>     operator interface, motor-current sensing, expanded I/O.
> - **Camera:** replace the BOM's Hikvision DS-2CD2043G2-I (2.8 mm, ALPR
>   range only about 3–5 m) with a 4 MP motorised varifocal per side
>   (candidate DS-2CD2643G2-IZS, 2.8–12 mm; verify availability and price
>   in Kenya).
>   - The main stream feeds ALPR and MMYRC; the sub-stream feeds the app.
>   - The server restreams via MediaMTX, WebRTC first, HLS as fallback.
> - **Deployment:** design for a common Kenyan homestead (one gate, a family,
>   phones only, unreliable power). One GPU server (estate or hosted) can
>   serve many gates/stations. RTX 20 series and newer.
> - **App:**
>   - a floating bottom nav bar on Android AND iOS; Android currently shows
>     no nav bar at all on a Pixel 7;
>   - a live stream window per gate;
>   - a 2.5-D photoreal gate animation driven by the real position (limits
>     + travel time), per gate type;
>   - a Controller tab: CPU, heap, uptime, firmware, link, relay states and
>     cycle counts, limits/beam history, run time vs expected, motor
>     current, faults, OTA, temperature;
>   - gate type selection (admin, gate closed) with verify + calibrate;
>   - in Vehicles, an allow/block dropdown per vehicle (admin only);
>   - guest passes: a single visit or valid until a time, per gate(s),
>     with a guard notification on arrival and an alert on a make/model
>     mismatch once MMYRC exists;
>   - stations: several stations kept in Settings, each fully isolated with
>     its own sign-in, one station shown at a time, and NO server field on
>     the sign-in page.
> - **MMYRC (make, model, year range, colour):**
>   - global, with full Kenyan-market coverage;
>   - year as a generation range;
>   - trained on the best public dataset for now;
>   - a multi-task model (shared backbone, separate heads);
>   - early LiDAR→camera fusion (point-cloud projection);
>   - NO reverse web search;
>   - for an unrecognised vehicle, alert the guard and admin with its
>     image (kept 30 days or until labelled); the admin's label goes into
>     an embedding gallery so similar vehicles are recognised next time;
>   - RTX 20+.
>
> **Spec decisions 2026-10-31** (docs/design/gate-system-v2.md, unreviewed):
> mechanical release on every bolt; keyed override outside for outages; 24 V
> locks; exit button beats lockdown; force-open of a blocklisted vehicle is
> admin only (reason + alert); unknown vehicles exit by button or app. Local
> server per site, plus one remote oversight server on a Google Cloud VM: see
> spec §25. Build it after step 7. Next: senior review of the spec.
>
> **DONE 2026-10-30:**
> - Models committed and pushed (c2a176f..a6d5aad).
> - KiCad MCP server installed at `C:\\Users\\User\\tools\\KiCAD-MCP-Server`
>   (venv on KiCad's Python) and registered at user scope as `kicad`; give
>   it Windows paths.
> - Next: the user restarts Claude Code, then step 2 (nav fix).
>
> (Was pending at the next natural break: install the KiCad MCP server.)
> 1. `git clone --branch stable https://github.com/mixelpixx/KiCAD-MCP-Server.git ~/tools/KiCAD-MCP-Server`
>    (v2.8.3, MIT).
> 2. `npm install && npm run build`, then `setup-linux.sh`.
> 3. Register it in the USER-scope Claude Code config, available system-wide
>    (`claude mcp add --scope user …`), NOT in the repo.
> 4. Then ask the user to restart Claude Code.
>
> Use it for the board rev 3 work (step 5): placement, DRC/ERC, JLCPCB
> parts, renders. Keep the git checkpoints and senior review.
>
> **Sequence:**
> 1. Finish and commit the models work (§6).
> 2. App floating nav fix.
> 3. System design spec, reviewed.
> 4. Firmware, server, proto and simulator (incl. blocklist and guest-pass
>    APIs, controller telemetry).
> 5. Board rev 3 + enclosure.
> 6. App features.
> 7. MMYRC.
>
> **Update 2026-10-30 (latest): the APP IS DONE AND COMMITTED.**
> - The reviewer gave final APPROVE. Seven commits are pushed to `develop`
>   (87bf743 … a38a711), on top of 422517f.
> - Only `app/docs/phase2b-evidence/{smoothness,theme}/` stay local
>   (untracked, intentionally).
> - CI on a38a711 is being watched; the App workflow builds Windows and
>   Android and runs the Core tests.
> - **Models engineer resumed (§6):** the plugin-logger lifetime fix, the
>   manifest tidy, then re-running `commit_groups_stage_build.sh` against
>   the new HEAD, then commit groups a–g.
> - **Tell the user (approved wording):** "Smooth and confirmed on Windows,
>   on this machine, on mains power, measured with the instrumented Release
>   build. Run F: all 11 page entrances median 0 dropped frames (worst 1 in
>   2 of 55 runs), no frame over 28 ms; tap → nav pill median 4 ms. Theme
>   wipe starts in about 30–40 ms with 0 dropped frames; one start in 20
>   took 112 ms. Android is built but not run-tested."
> - **Disclose with it:**
>   - the residual background gen-2 risk;
>   - no human Narrator pass, and no UIA Invoke pattern on the pressables;
>   - the cancel paths were tested via the harness only (worth a 30 s
>     manual check);
>   - simulator latch refusals;
>   - dotnet-trace installed;
>   - cleanup of `%TEMP%\gctrace|gcburst|gcparse`,
>     `%LOCALAPPDATA%\GateControl\stage*` and
>     `%LOCALAPPDATA%\GateControl\commit-staging`.
>
> **Update 2026-10-30 (later):**
> - **Reviewer's FINAL verdict on app round 3:** APPROVE WITH CHANGES.
> - **The app engineer is now doing:**
>   1. Persist the appearance preference only on tap or wipe completion,
>      not on press.
>   2. Roll back immediately on PointerCanceled, PointerExited or a drag
>      past the threshold, then re-run the theme rows and edge cases.
>   3. A final-code resize/theme SettleXamlCleanup run.
>   4. The keyboard-reachability regression from 2a (NavCapsule and the
>      Pressables are not focusable): focus, Enter/Space, focus visuals,
>      UIA roles; re-run acceptance as Run F.
>
>   Items 1–4 form a separate **group 7** on top of groups 1–6. The plan:
>   stage-build 7 groups, then commit.
> - **Approved wording for the user,** after the fixes: "Smooth and
>   confirmed on Windows, on this machine, on mains power, measured with the
>   instrumented Release build; Android built, not run-tested."
> - **Also disclose:**
>   - the residual background gen-2 risk;
>   - no human Narrator pass;
>   - the glow approach;
>   - simulator latch refusals;
>   - the dotnet-trace tool install.
>
> **Update 2026-10-30:**
> - **The app round-3 pass is complete and reported:**
>   - **Theme reveal:** all checks pass. Fixed a corrupt snapshot caused by
>     the off-window parked tabs, moved the theme switch to pointer-press,
>     and removed a mid-wipe GC.
>   - **Run E:** all entrances 0/0.
>   - **Items 0–5:** done, via the new `Controls/ParkHost.cs` (focus and UIA
>     pruning for parked tabs).
>   - **Commit groups:** built in order (`app/tools/commit_groups_stage_build.sh`,
>     results in `round2/builds/commit-groups-stage.txt`). The updated file
>     lists move `PlatformServices` and `Automation` to group 4.
> - **Awaiting:** the reviewer's FINAL verdict, then commit groups 1–6,
>   then resume the models engineer.
> - **Leftovers for the user:** a human Narrator pass; low-end GPUs
>   unmeasured; temp folders `%TEMP%\gctrace|gcburst|gcparse` and
>   `%LOCALAPPDATA%\GateControl\stage*`.
>
> **Update 2026-10-29 (latest):**
> - **Reviewer's interim verdict on round 3:** APPROVE WITH CHANGES. Run D
>   is trusted.
> - **The app engineer is now doing, in one pass:**
>   - the user-requested ThemeReveal re-check (origin, latency, 500 ms
>     coverage, wavy edge, drops, snapshot correctness, interruptions,
>     Reduce Motion, the stale-press discard);
>   - noting that Run D used the harness build;
>   - keyboard focus must not enter parked tabs;
>   - the UIA tree and Narrator must exclude parked tabs;
>   - WinTransform must clear its inner-view transform when a container
>     appears later;
>   - anchor and RTL parity vs stock MAUI;
>   - SettleXamlCleanup re-armed after resize, maximise and theme change;
>   - per-group worktree build verification.
> - **Then:** the reviewer's final verdict, then commit.
>
> **Update 2026-10-29 (later):**
> - **App round 3 report received.** Run D, on mains, has **every acceptance
>   row passing**: median 0 / worst 0, longest frame ≤ 24.8 ms, and tap →
>   pill median 4.3 ms.
> - **GC causes fixed:** WinTransform, the native BlobClip clip, the
>   ChartView text cache, and `RootPage.SettleXamlCleanup` for WinUI's
>   TrackMemory GC. SustainedLowLatency and HighMemoryPercent are removed.
> - **Residual risk:** the frame ticker's event-args wrappers can still cause
>   an occasional background gen-2.
> - **Next:** the reviewer's final check, then commit the six app groups (the
>   file lists are in the round-3 report), push, watch CI, then resume the
>   models engineer (§6).
>
> **Update 2026-10-29:**
> - **What happened on 2026-10-24 (17:56–20:05):** a further pass changed
>   these files (author unconfirmed: possibly another agent run from this
>   file):
>   - `RootPage.cs`, `BlobClip.cs`, `Reveal.cs`, `WinTransform.cs`,
>     `Automation.cs`, `MauiProgram.cs`, the csproj;
>   - motion-capture tools;
>   - spec §17;
>   - `acceptance-table.md`, Run C.
> - **Run C still FAILS the pass rule:**
>   - Dashboard first visit: median 3 dropped.
>   - Reports first and later visits, and Dashboard later visit: median 1.
>   - Alerts first visit: worst 3.
>
>   Spec §17 is wrongly titled "completed".
> - **Now:** the app engineer is resumed to verify those changes, find the
>   remaining Induced GC, fix the failing rows, re-run the acceptance on
>   mains power, and correct §17.
> - **Still uncommitted.** Models are still paused until the app is approved
>   and committed.
>
> Previous update, 2026-10-24 (later): app Phase 2b round 2 report received:
> items 2 and 4–9 done, smoothness acceptance still failing; awaiting the
> reviewer's verdict. Models Phase 2 final fixes paused.
>
> **Latest app status (overrides §5 where they differ):**
> - **Done:** items 2 and 4–9, with evidence in
>   `app/docs/phase2b-evidence/round2/` (including `builds/`). Core tests
>   105/105. Windows and Android, Debug and Release, all 0 warnings / 0
>   errors. Harness strings are absent from Release.
> - **Item 3 (smoothness) not passing.** Run A (mains power) fails only these
>   first visits:
>   - Dashboard first visit fails because one blocking full GC lands 100–700
>     ms into the first reveal after launch, with about 3 MB allocated. The
>     cause is not found yet.
>   - Reports and Vehicles first visits failed on a run that hadn't
>     preloaded. A 2 s per-tab preload cap was added afterwards.
>
>   Run B was on battery and is invalid. Re-run on mains power with
>   `app/tools/motion-capture/acceptance.sh 5` then `acceptance.py 5`.
> - **Proposed commit groups:** listed in the app engineer's round-2 report.
>   They are §5's six groups with exact paths; `smoothness/` (8.6 MB) stays
>   out and `theme/` is optional.
> - **Reviewer verdict on round 2: APPROVE WITH CHANGES.**
>   - **Accepted:** items 2 and 4–9.
>   - **Run B:** invalid as a pass; report it as informational only.
>   - **Harness GC diagnostics:** keep them; they're compiled out of Release.
>
>   The app engineer is now doing the following, all before the models
>   engineer resumes:
>   1. Remove the forced blocking gen-2 GC on every tab switch
>      (`RootPage.cs:388`). Keep at most a single idle, non-compacting
>      collection after pre-warm, and only if a trace shows it helps.
>   2. Find the first-entrance GC trigger with
>      `dotnet-trace collect -p <pid> --providers Microsoft-Windows-DotNETRuntime:0x1:5`.
>      Report Reason, Depth, Type and the preceding allocation, then fix by
>      cause (likely native bitmap memory pressure). NoGCRegion or
>      SustainedLowLatency alone is not an acceptable fix.
>   3. Make `HighMemoryPercent=97` Windows-only, or remove it if the trace
>      shows it isn't needed.
>   4. Run the 5-run acceptance on **mains power only** (check `PowerOnline`
>      first), plus a new row: tap → first pill-motion frame ≤ 17 ms.
>   5. Update the spec, README and `acceptance-table.md`. State any remaining
>      failure plainly.
>
>   After that: the final review, then commit groups 1–6 (group 6 without
>   `smoothness/` and `theme/`). Then resume the models engineer (§6).
>
>   Only tell the user "smooth" when every row passes, scoped as
>   "Windows, this machine, on mains power; Android built but not
>   run-tested".

---

You are continuing a multi-day engineering effort on the repository at
`/home/deby/projects/gate-automation` (WSL2 Debian on Windows; the user is
Ian Kiogora Mwenda). Carry on as if this were one continuous execution: do
not restart work that is already done, do not re-litigate decisions recorded
below, and keep the same working style, logging and documentation habits.

## 1. What the project is

An ALPR + LiDAR gate-automation system for Kenyan sites:
- `server/`: C++20 gRPC `gate-server` (fusion, allowlist, auth), `server/inference`
  (TensorRT ALPR pipeline) and `server/vision` (`gate-vision` daemon: camera,
  LiDAR, submit).
- `dashboard/backend`: Drogon HTTP/WebSocket API with a SQLite history store.
- `firmware/`: ESP-IDF ESP32-S3 field controller.
- `simulation/`: `gate-sim`, a virtual gate.
- `app/`: **Gate Control**, a .NET 10 MAUI operator app (Android, iOS, Mac
  Catalyst, Windows). It replaced the old web dashboard (ADR-013).

## 2. How the work is organised (keep doing it this way)

The user asked for parallel workers plus a **senior reviewer** who checks
each phase before the next one starts:
- **App engineer:** implement the UI/UX and all animations from the reference
  video `C:\Users\User\Videos\WhatsApp Video 2026-10-19 at 13.25.09.mp4`
  (`/mnt/c/Users/User/Videos/...` from WSL).
- **Models engineer:** select, evaluate and integrate plate detection and OCR
  models for Kenyan plates.
- **Senior reviewer:** verify claims independently (run tests, open the
  evidence images, read the diffs), then reply `VERDICT: APPROVE / APPROVE
  WITH CHANGES / REJECT` with numbered required changes.

**Current user instruction:** the app engineer finishes ALL its work first
(including the reviewer's sign-off). Only then does the models engineer
resume. If you are a single agent, play the roles in sequence: do the work,
then do a genuinely skeptical review pass against the checklists below
before you call anything done.

Rules every role follows:
- Workers never commit. Commits happen only after reviewer approval, in the
  planned logical groups (sections 5 and 6), on branch `develop`.
- Commit messages are conventional-commit style (see `git log`), explain the
  *why*, and end with exactly:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01RFTaQTzDxoWNYx3YEgYvNr
  ```
  If you are not Claude, replace those two lines with your own tool's
  attribution, and say so to the user.
- Push to `origin develop` after committing. Then watch CI (`gh run list
  --branch develop`) and fix failures. The CI history and its lessons are in
  section 8.
- Report honestly: give failing numbers as they are, state skipped steps, and
  never claim "smooth" or "passes" without measurements.
- Never change the user's system settings or VMs (VirtualBox is installed),
  and never touch hardware/PCB files.
- Never kill processes with a `pkill` pattern that matches your own command
  line. Kill demo processes by their `/proc/*/exe` path.
- Write evidence to the paths named below and keep the docs updated.

## 3. Repository state at handoff

- HEAD is `422517f` on `develop` (CI fully green there). **Everything below is
  uncommitted in the working tree.** Do not discard it.
- About 66 changed files under `app/src`, plus `app/tests`, `app/docs`,
  `app/tools` and `app/README.md`: the app redesign.
- About 40 files under `server/inference`, `server/vision`,
  `scripts/export-models`, `tests/inference`, `tests/CMakeLists.txt`,
  `server/CMakeLists.txt`, `docs/decisions` (ADR-014, ADR-010),
  `docs/commissioning.md`, `deployment/`, `server/models/README.md` and
  `README.md`: the models integration.
- Git-ignored artefacts (never commit them):
  - `models/weights/engines/{yolov9_plate,plate_ocr}.plan` with `.meta`,
    plus `plate_ocr_config.yaml`;
  - `models/downloads/` (detectors, recognizers, the Kenya test-set images,
    and `eval/` evidence including `commit_groups_stage_build.sh`).

## 4. Environment facts

- C++ build:
  - CPU: `cmake --preset debug-cpu && cmake --build build/debug-cpu -j8 &&
    ctest --test-dir build/debug-cpu -j8` (expected 236/236 with the models
    work).
  - GPU tree: `--preset debug` → `build/debug` (expected 252/252).
  - Sanitizer presets: `asan` (instrumented vcpkg triplet
    `triplets/x64-linux-asan`) and `ubsan`.
- Lint: `clang-format --dry-run --Werror` on touched files; `run-clang-tidy
  -p build/debug-cpu` per `.clang-tidy`.
- GPU: RTX 5060 Laptop (sm_120), CUDA 13.1, TensorRT 10.16.1 (`trtexec` at
  `/usr/bin`, no TRT Python bindings), onnxruntime-gpu 1.24.3 (call
  `onnxruntime.preload_dlls()`), torch 2.12 cu128, ultralytics 8.4.21,
  paddleocr 3.4.1. paddle2onnx needs a Python 3.12 venv.
- Firmware: `source ~/esp/esp-idf/export.sh && cd firmware && idf.py build`.
- **App build loop:**
  - The app is built from Windows through drive `W:` (mapped to
    `\\wsl.localhost\Debian`), using the Windows dotnet:
    ```
    "/mnt/c/Program Files/dotnet/dotnet.exe" test  'W:\home\deby\projects\gate-automation\app\tests\GateControl.Core.Tests'
    "/mnt/c/Program Files/dotnet/dotnet.exe" build 'W:\home\deby\projects\gate-automation\app\src\GateControl.App' -f net10.0-windows10.0.19041.0
    "/mnt/c/Program Files/dotnet/dotnet.exe" build 'W:\...\GateControl.App' -f net10.0-android
    ```
  - `app/Directory.Build.props` redirects bin/obj to
    `%LOCALAPPDATA%\GateControl\build`, because WinUI won't start from a
    network drive. The exe is at
    `C:\Users\User\AppData\Local\GateControl\build\bin\GateControl.App\Debug\net10.0-windows10.0.19041.0\win-x64\GateControl.exe`.
  - Measurement builds use `-p:GcHarness=true`. The DEBUG automation harness
    (`Platform/Automation.cs`) drives the app from a command file; it must
    stay out of Release.
  - Capture and analysis tools live in `app/tools/motion-capture/`. UI
    scripts (`ui.ps1` UIA, `shot.ps1` PrintWindow) live in
    `/tmp/claude-1000/-home-deby-projects-gate-automation/2ee2d9ba-0769-44f3-a6a1-47be00738d3e/scratchpad/winscripts/`
    and may be gone; recreate them if needed.
- **Demo backend:**
  - Start it with `scripts/demo-stack.sh --hardened`: user `demo-admin`,
    password `demo-open-sesame`, at `http://127.0.0.1:8080`. Also created
    were users `viewer1` and `operator1`.
  - From Windows use `127.0.0.1`, never `localhost` (IPv6 stalls on the WSL
    relay).
  - gate-sim supports only OPEN/CLOSE; latch commands return
    `success=false` "unsupported on the simulated residential profile".
- **XAML gotcha:** with `MauiXamlInflator=SourceGen`, compiled
  `RelativeSource AncestorType` bindings in item templates throw a
  NullReferenceException. Use `{Binding Vm.X, Source={x:Reference Page},
  x:DataType=p:XPage}` with a typed `Vm` property instead.
- **Android:** neither emulator boots on this host (likely a VirtualBox /
  Hyper-V conflict) and no device is attached. Android is "built, not
  run-tested". Ask the user for a phone (USB or wireless debugging) or for
  permission to fix the hypervisor conflict.

## 5. App workstream: where it stands, what is left

**The design spec:**
- `app/docs/design-video-spec.md` (rev 2, plus §14 for 2a and §15 for 2b,
  with a deviation table).
- Reference frames are in `app/docs/design-video-frames/`.
- The video is a 5.93 s showcase of a desktop web dashboard ("Vertix"). Its
  look and motion are mapped onto our pages; none of its strings or figures
  ship.

**Decisions already made (do not reopen them):**
- **Font:** Inter is bundled; a tabular-figures face is used for numbers only.
- **Colour:** ink for selection and primary actions; orange `#E15A41` only
  for brand, accent tile, hero, toggles and dots. No blue anywhere; caution
  is `#F5A524`.
- **Nav:** a content-width three-part nav pill on one spring pair, inside a
  persistent `RootPage` shell in a `NavigationPage`.
- **Theme reveal:** snapshot plus a wavy disc clip (48 points,
  r·(1+0.06·sin(5θ+φ))). Capture on pointer-down; first frame ≤ 100 ms;
  coverage 500 ± 33 ms (curve 0.62 s, accepted).
- **Count-up:** a timed ease-out-cubic over 1.45 s. The theme reveal is the
  only other non-spring motion.
- **Blur:** Android 12+ `RenderEffect`, off by default until measured on a
  device. Elsewhere, fade plus an 8-dip rise plus a 0.96 scale.
- **User requests:** the theme toggle sits at the **top right** (the wipe
  starts there), and **page-load animations must be smooth with measured
  proof**.

**Phase 2b round 2: the reviewer's required changes.** Status as read from the
working tree on 2026-10-24:
1. **Smoothness, NOT yet passing.** Acceptance means 5 runs per entrance (9
   entrances plus 2 Reduce Motion), and for each: median 0 dropped frames
   (60 Hz budget), worst ≤ 1, and no frame over 33 ms inside the reveal
   window. Do not stop the user's VMs; note host load instead.
   - The last table, `app/docs/phase2b-evidence/round2/acceptance-table.md`,
     passes only 4/11. Dashboard and Reports fail on both first and later
     visits (median 3–4 dropped, frames 48–77 ms); Alerts first, Live later
     and Vehicles later fail on outliers.
   - The cause analysis is in `round2/long-frame-attribution.md`.
   - The agent was in the middle of implementing a "prewarm render and
     staged tail restore": parked pages pre-measured and arranged at the
     live window size; BindableLayout items batched before the reveal;
     chart redraws kept out of the reveal window; the reveal clock started
     on the first presented frame after the swap, with content at opacity 0.
   - Finish that work, re-run all 55 runs, and regenerate the table.
2. **Smoothness, part two.** Done: the switch-frame pre-warm, part of item 1.
3. **Smoothness, part three.** Not done: the 5-run acceptance in item 1.
4. **Gate-command failure status.** Appears done: `round2/gate-hold.png`
   and `round2/gate-latch-status.jpg`. Confirm the error-haptic path runs.
5. **Orange glow restored.** Appears done (`accent_glow.png`). It is a
   pre-rendered image behind the tile, opacity-only after the blob reveal.
   A frame-time proof that it adds no long frames is still needed.
6. **New users default to Viewer.** Done in `SettingsViewModels.cs`, with a
   test. Prove that choosing Viewer through the UI picker works.
7. **Android.** Still needed:
   - Debug and Release builds at 0/0, logs attached;
   - a code read of every `#if ANDROID` branch (PlatformBlur, ThemeReveal
     capture, haptics, Reduce Motion detection);
   - README and spec saying "Android: built, not run-tested; needs a device";
   - Windows Debug and Release build logs, and the harness confirmed absent
     from Release (search the Release dll for `GC_AUTOMATION_FILE`).
8. **Ghosting.** Settled screenshots for every tab, light and dark, exist in
   `round2/`. Confirm no ghosting, and that parked pages are not drawn after
   their exit.
9. **Polish.** Hour labels on the x axis of the Vehicles-by-hour chart; the
   empty Dashboard grid cell filled. Clearing a single alert is re-verified
   (`round2/alerts-after-clear-one.png`).

**Then:**
- Write the 2b report (with a "Page-load smoothness" median/worst table)
  and get a skeptical review.
- Run the final regression on the demo stack:
  - gate commands;
  - feed drop-in;
  - search, paging and CSV;
  - plate history;
  - roles viewer/operator/admin;
  - alerts badge;
  - sign-out/in;
  - no picker crashes.
- Commit `app/` only, in these groups:
  1. tokens + fonts/icons + licences;
  2. Core motion/formatting + tests;
  3. App motion primitives + controls;
  4. shell (`RootPage`, NavCapsule/Rail; deletes `AppShell.cs`);
  5. pages + view models;
  6. docs/spec + small evidence (keep big frame bursts out).
- Each group must build. Then push and watch CI: the `App` workflow builds
  Windows and Android and runs the Core tests.

## 6. Models workstream: paused, nearly commit-ready

**Results (tell the user these plainly):**
- **Detector:** YOLOv9-s-608 (open-image-models). Weights are treated as
  **GPL-3.0**; training data is web-scraped with unknown provenance.
  RT-DETRv2 (Apache-2.0) is the documented fallback. The AGPL YOLOv11 is
  rejected.
- **Recognizer:** fast-plate-ocr CCT-S v2 (MIT), integrated as a
  fixed-slot `PlateRecognizer`.
- **End-to-end exact match** after Kenya normalisation, C++, on 225 plates in
  166 Wikimedia Commons photos: **0.73** (one-line 0.75, two-line **0.68**).
- **Latency:** 8.5 ms median per frame.
- **Fine-tuning** on site-collected crops (2–5k, weighted to two-line and
  KMxx plates) is a documented follow-up in ADR-014. It is out of scope now.

**The reviewer's final check said APPROVE WITH CHANGES. Remaining:**
1. **Plugin logger lifetime** (use-after-free risk). `initLibNvInferPlugins(&logger, "")`
   keeps the logger pointer for the life of the process, but the first caller
   passes a logger owned by `CameraSource` (`camera_source.cpp` ~51/60) or a
   stack logger in tests and alpr_bench. Pass a process-lifetime logger
   instead, e.g. a function-local `static TrtLogger plugin_logger;` inside
   `register_standard_plugins` in `server/inference/src/trt_engine.cpp`.
   Rebuild, then re-run the GPU ctest and the 166-image parity check
   (`scripts/export-models/eval/cpp_parity.py`); both should be unchanged.
   This belongs to group (a).
2. **Manifest tidy** (group e), in
   `scripts/export-models/eval/testset/manifest.csv`:
   - strip Commons boilerplate from author fields (e.g. "Hansueli Krapf
     \xa0This file was uploaded with Commonist." → "Hansueli Krapf");
   - optionally drop `utm_*` query parameters;
   - add a note that images are not redistributed, each image is under its
     listed licence and author, and our annotations are under the project
     licence.

The agent had started this round and was running the GPU suite when it
paused. Check the state of `trt_engine.cpp` before redoing anything.

**Then:**
- Re-run `models/downloads/eval/commit_groups_stage_build.sh`. It stages the
  groups cumulatively into a detached worktree and builds and tests each.
  Check that the final staged tree equals the working tree.
- Commit in order **(a) → (b) → (c) → (d)/(f) → (e) → (g)**:
  - **(a)** TrtEngine: runtime dlopen of `libnvinfer_plugin.so.10`, the
    plugin error message, the logger fix, tidy renames;
    `deployment/install-server.sh` plugin check; in
    `server/inference/CMakeLists.txt`, only the plugin comment and
    `${CMAKE_DL_LIBS}`.
  - **(b)** `gate_plate_text` (`plate_text.hpp/.cpp`) + `tests/inference/plate_text_test.cpp`
    + `server/CMakeLists.txt`, `tests/CMakeLists.txt`,
    `tests/inference/CMakeLists.txt`.
  - **(c)** `plate_recognizer.*` added; `paddle_ocr_recognizer.*` deleted;
    `alpr_pipeline.*`, `detail/cpu_algorithms.*`, `cpu_algorithms_test.cpp`,
    `server/vision/src/camera_source.cpp`; the CMake source swap and the
    gate_plate_text link.
  - **(d)** `yolo_plate_detector.*` (input size from the engine, floor 0.15),
    `tools/alpr_bench.cpp`, the CMake alpr_bench block.
  - **(e)** `scripts/export-models/**` (fetch, graft, prepare, eval,
    testset manifest; no `__pycache__`).
  - **(f)** `server/vision/src/gate_vision_main.cpp`,
    `deployment/config/gate-vision.env`, `docs/commissioning.md`.
  - **(g)** ADR-014, ADR-010 amendment, `README.md`,
    `server/models/README.md`.
- Dependencies: c needs b; d and f need c. `server/inference/CMakeLists.txt`
  is split across a, c and d. Every commit must build and pass the CPU tests.

## 7. Open items for the user (surface these in your final summary)

- **Android device testing:** needs a phone or a hypervisor change.
- **Simulator:** latch commands (Hold/Lockdown/Release) aren't simulated.
  Adding them to `VirtualGate` is a separate follow-up.
- **Models:**
  - site data collection for fine-tuning;
  - two-line plates read correctly only about 68% of the time;
  - the detector licence and data-provenance risk.
- **Hardware:** the PCB rev 2.x bench test (physical work).

## 8. Lessons from CI (don't relearn them)

- The configure guard matches only `Skipping X: ... not found`. The
  `server/inference` skip on CPU builds is intentional.
- Hosted runners run out of memory when linking several gRPC binaries in
  parallel. `GATE_LINK_JOBS` caps link jobs (1 for sanitizers, 2 for builds).
- ASan needs instrumented dependencies (`x64-linux-asan` with
  `grpc[codegen]`). Otherwise abseil reports false use-after-poison errors.
- A cold vcpkg build takes about 70 minutes (about 2 hours for asan). The
  cache is saved right after configure.
- Clang 19 and later need `-Wno-nullability-extension` on protoc output.

## 9. How to log and document as you go

- After each phase, write a report with these sections: what changed (files),
  build and test output summaries, evidence paths, measured results vs
  targets in a table, deviations, and what's not done and why.
- Keep `app/docs/design-video-spec.md` (deviation table, §15+),
  `app/docs/phase2b-evidence/`, `models/downloads/eval/RESULTS.md` and the
  ADRs current.
- Update this file (`docs/handoff/CONTINUE.md`) at every milestone, so the
  next agent can pick up from it.
- At the end, give the user a short, plain summary: what's done, what's
  verified, what still needs them.
