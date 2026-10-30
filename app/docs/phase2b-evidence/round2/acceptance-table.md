# Page-load smoothness — acceptance (5 launches per entrance)

Pass rule (review round 2): **median 0 dropped frames, worst ≤ 1, and no frame over 33 ms inside the
reveal window**, per entrance over 5 launches; plus **tap → first pill-motion frame ≤ 1 frame
(≤ 17 ms)**. Dropped = frames missed against a 60 Hz budget (the panel runs at 240 Hz), counted
over the whole 1.8 s trace (0.9 s for Reduce Motion), not only the reveal window.
Release build with the harness compiled in (`-p:GcHarness=true`), window 1500×960, demo stack
`--hardened`. Tool: `app/tools/motion-capture/acceptance.sh 5` then `acceptance.py 5`.
Each run signs out and back in first and is repeated if any trace is not a real entrance
(`acceptance.py --check`): in an earlier run the app fell back to the sign-in page mid-run and its
later "entrances" were empty traces that scored 0 dropped.

Tap → first pill-motion frame: from the `tab.select` mark (where a tap lands, RootPage.Select)
to the first frame tick after the pill's first move (`nav.apply`), every non-Reduce-Motion
entrance. (Run C's version of this row measured trace start → first frame, not the tap.)

**Build measured (review round 3, item 0):** every run in this file used the **Release build with
the verification harness compiled in** (`dotnet build -c Release -p:GcHarness=true`): the in-app
trace samples every frame and records spans, and the harness keeps the frame ticker running for
the trace. The shipped Release binary has none of that (`builds/release-harness-check.txt`) and
does less work per frame; it was not traced separately.

## Run F — 2026-10-30 09:36–09:43, on mains power (PowerOnline: True) — final code with group 7: ALL ROWS PASS

Group 7 = theme press fixes + keyboard accessibility. Host: `vmwp` (the WSL VM) running, 88 % committed
memory, 14 % CPU before the run. Long frames (> 25 ms): 2 in 45 entrances (both compositor, 28 ms
and 26 ms, each 1 dropped frame in one run; median 0, worst 1).

| Entrance | runs | median dropped | worst dropped | per run | longest frame in reveal window (ms) | reveal window (ms, median) | result |
|---|---|---|---|---|---|---|---|
| Dashboard, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 19.6 | 1182 | pass |
| Reports, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 23.3 | 941 | pass |
| Vehicles, first visit | 5 | 0 | 1 | 1, 0, 0, 0, 0 | 19.3 | 844 | pass |
| Alerts, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 11.6 | 940 | pass |
| Live, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 20.5 | 823 | pass |
| Dashboard, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 17.9 | 1182 | pass |
| Reports, later visit | 5 | 0 | 1 | 0, 0, 1, 0, 0 | 28.0 | 942 | pass |
| Vehicles, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 20.9 | 841 | pass |
| Alerts, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 8.3 | 942 | pass |
| Dashboard, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 10.9 | 0 | pass |
| Vehicles, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 11.2 | 0 | pass |
| Tap → first pill-motion frame (ms) | 45 | 3.9 | 10.5 | (all 45 entrances) | — | — | pass |

### Appendix — SettleXamlCleanup with the final code (review round 3 / final, item 3)

`tools/motion-capture/settle_check.sh`, 3 rounds: resize → enter a tab, maximise / restore →
enter, theme change → enter; plus a tab switch 0.75 s after a resize (during the nudge).
gen-2 counts before → after each traced entrance, and every frame over 25 ms:

```
SR-interrupt: gen2 12->12 gen0 18->18 frames>25ms [] settle-starts-in-trace 0
SR1-maxrestore: gen2 10->10 gen0 14->14 frames>25ms [] settle-starts-in-trace 0
SR1-resize: gen2 9->9 gen0 13->13 frames>25ms [] settle-starts-in-trace 0
SR1-theme: gen2 10->10 gen0 14->14 frames>25ms [] settle-starts-in-trace 0
SR2-maxrestore: gen2 11->11 gen0 16->16 frames>25ms [] settle-starts-in-trace 0
SR2-resize: gen2 10->10 gen0 15->15 frames>25ms [] settle-starts-in-trace 0
SR2-theme: gen2 11->11 gen0 16->16 frames>25ms [] settle-starts-in-trace 0
SR3-maxrestore: gen2 12->12 gen0 18->18 frames>25ms [] settle-starts-in-trace 0
SR3-resize: gen2 11->11 gen0 16->16 frames>25ms [] settle-starts-in-trace 0
SR3-theme: gen2 12->12 gen0 18->18 frames>25ms [] settle-starts-in-trace 0
```
0 gen-2 collections and no frame over 25 ms in any of these entrances.

## Run E — 2026-10-30 00:29–00:36, on mains power (PowerOnline: True) — groups 1–6 code: ALL ROWS PASS (superseded by run F)

Code of groups 1–6: parked tabs wrapped in `ParkHost` (keyboard focus and UI Automation
kept out of them), tab-stop changes at idle via the MAUI tree, the host clipped only during a
theme reveal, the theme switch run during the press, settle re-armed on resize. Host: `vmwp`
(the WSL VM) running, 79 % committed memory, 13 % CPU before the run. Long frames (> 25 ms): 0 in
45 entrances.

| Entrance | runs | median dropped | worst dropped | per run | longest frame in reveal window (ms) | reveal window (ms, median) | result |
|---|---|---|---|---|---|---|---|
| Dashboard, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 20.4 | 1181 | pass |
| Reports, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 22.8 | 941 | pass |
| Vehicles, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 17.2 | 843 | pass |
| Alerts, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 8.1 | 941 | pass |
| Live, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 20.2 | 822 | pass |
| Dashboard, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 21.3 | 1182 | pass |
| Reports, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 22.7 | 940 | pass |
| Vehicles, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 18.3 | 843 | pass |
| Alerts, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 8.4 | 941 | pass |
| Dashboard, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 12.0 | 0 | pass |
| Vehicles, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 9.3 | 0 | pass |
| Tap → first pill-motion frame (ms) | 45 | 4.0 | 8.7 | (all 45 entrances) | — | — | pass |

## Run D — 2026-10-29 12:31–12:38, on mains power (PowerOnline: True) — code of round 3: ALL ROWS PASS (superseded by run E)

Host: Hyper-V worker `vmwp` running (the WSL VM); 72 % committed memory, 17 % CPU before the run.
No VM or system setting was changed.

| Entrance | runs | median dropped | worst dropped | per run | longest frame in reveal window (ms) | reveal window (ms, median) | result |
|---|---|---|---|---|---|---|---|
| Dashboard, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 22.7 | 1182 | pass |
| Reports, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 24.8 | 942 | pass |
| Vehicles, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 20.2 | 842 | pass |
| Alerts, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 9.7 | 941 | pass |
| Live, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 21.5 | 823 | pass |
| Dashboard, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 22.7 | 1181 | pass |
| Reports, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 24.7 | 939 | pass |
| Vehicles, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 19.8 | 841 | pass |
| Alerts, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 10.0 | 943 | pass |
| Dashboard, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 13.2 | 0 | pass |
| Vehicles, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 11.4 | 0 | pass |
| Tap → first pill-motion frame (ms) | 45 | 4.3 | 10.9 | (all 45 entrances) | — | — | pass |

Long frames (> 25 ms) attributed: 0 in all 45 entrances (`long-frame-attribution.md`).

## Run D0 — 2026-10-29 12:16–12:23, on mains power — same app code as D, harness differed: 1 FAIL

| Entrance | runs | median dropped | worst dropped | per run | longest frame in reveal window (ms) | reveal window (ms, median) | result |
|---|---|---|---|---|---|---|---|
| Dashboard, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 23.0 | 1188 | pass |
| Reports, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 24.2 | 948 | pass |
| Vehicles, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 18.0 | 847 | pass |
| Alerts, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 8.8 | 946 | pass |
| Live, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 20.4 | 826 | pass |
| Dashboard, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 20.9 | 1188 | pass |
| Reports, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 24.8 | 949 | pass |
| Vehicles, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 17.6 | 846 | pass |
| Alerts, later visit | 5 | 0 | 2 | 0, 0, 2, 0, 0 | 11.3 | 948 | FAIL |
| Dashboard, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 9.9 | 0 | pass |
| Vehicles, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 8.6 | 0 | pass |
| Tap → first pill-motion frame (ms) | 45 | 4.0 | 6.2 | (all 45 entrances) | — | — | pass |

The one failing row: Alerts later visit, run 3 — a 58 ms frame at 1263 ms, after the reveal
window (948 ms) but inside the trace, from a gen-2 collection. A full-run EventPipe trace
(`gc-traces/FR3.txt`) showed the remaining trigger: GCTriggered InducedNotForced from the
CompositionTarget.Rendering event's per-frame WinRT event-args wrapper (memory pressure). The
harness had a second Rendering handler of its own; it now samples on the app's ticker (one
handler, as in production), and run D followed. The production app still creates one wrapper per
animated frame, so such a background collection can still occur, at half the previous rate;
none landed in run D's 45 entrances.

## Run C — 2026-10-24 20:03, on mains power — superseded (FAILS the pass rule)

Run by another agent from the hand-off. Five rows fail the rule (median 0 / worst ≤ 1 /
≤ 33 ms): Dashboard first, Reports first and later, Dashboard later, Alerts first. Its write-up
judged 25–27 ms frames against a 25 ms threshold; the rule counts dropped frames, and those rows
fail it. Its pill-latency row measured trace start, not the tap.

| Entrance | runs | median dropped | worst dropped | per run | longest frame in reveal window (ms) | reveal window (ms, median) | result |
|---|---|---|---|---|---|---|---|
| Dashboard, first visit | 5 | 3 | 3 | 3, 3, 3, 1, 0 | 67.7 | 1189 | FAIL |
| Reports, first visit | 5 | 1 | 1 | 1, 1, 0, 1, 0 | 25.9 | 945 | FAIL |
| Vehicles, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 16.0 | 845 | pass |
| Alerts, first visit | 5 | 0 | 3 | 0, 0, 0, 0, 3 | 13.4 | 947 | FAIL |
| Live, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 22.2 | 825 | pass |
| Dashboard, later visit | 5 | 1 | 1 | 1, 1, 0, 0, 1 | 25.3 | 1188 | FAIL |
| Reports, later visit | 5 | 1 | 1 | 1, 1, 0, 1, 0 | 27.2 | 947 | FAIL |
| Vehicles, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 17.2 | 846 | pass |
| Alerts, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 13.9 | 947 | pass |
| Dashboard, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 7.6 | 0 | pass |
| Vehicles, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 5.6 | 0 | pass |
| Tap → first pill-motion frame | 5 | - | - | 5.8, 8.0, 5.9, 6.1, 5.9 | 8.0 | 5.9 (median) | pass |


---

## Run B — 2026-10-24 17:32–17:37, laptop ON BATTERY (informational only)

The machine was discharging (PowerOnline False, 83 %). Every entrance, including Reduce Motion,
got slower (Reduce Motion's longest frame went 7 → 14 ms); 71 of 81 long frames are
compositor-only. Invalid as a pass verdict.

| Entrance | runs | median dropped | worst dropped | per run | longest frame in reveal window (ms) | reveal window (ms, median) | result |
|---|---|---|---|---|---|---|---|
| Dashboard, first visit | 5 | 5 | 8 | 5, 4, 6, 4, 8 | 94.7 | 1198 | FAIL |
| Reports, first visit | 5 | 2 | 4 | 2, 1, 3, 4, 2 | 40.2 | 951 | FAIL |
| Vehicles, first visit | 5 | 0 | 1 | 0, 1, 0, 0, 0 | 26.1 | 849 | pass |
| Alerts, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 18.7 | 951 | pass |
| Live, later visit | 5 | 5 | 6 | 6, 5, 5, 4, 6 | 39.6 | 827 | FAIL |
| Dashboard, later visit | 5 | 2 | 5 | 3, 1, 0, 2, 5 | 40.1 | 1191 | FAIL |
| Reports, later visit | 5 | 4 | 6 | 6, 4, 2, 4, 3 | 42.3 | 948 | FAIL |
| Vehicles, later visit | 5 | 0 | 2 | 0, 2, 0, 1, 0 | 29.4 | 845 | FAIL |
| Alerts, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 19.6 | 947 | pass |
| Dashboard, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 14.1 | 0 | pass |
| Vehicles, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 14.7 | 0 | pass |

---

## Run A — 2026-10-24 12:04–12:08, on mains power (baseline)

| Entrance | runs | median dropped | worst dropped | per run | longest frame in reveal window (ms) | reveal window (ms, median) | result |
|---|---|---|---|---|---|---|---|
| Dashboard, first visit | 5 | 2 | 4 | 0, 2, 1, 4, 2 | 60.1 | 1190 | FAIL |
| Reports, first visit | 5 | 0 | 27 | 27, 0, 0, 0, 0 | 262.0 | 947 | FAIL (run 1 not preloaded; fixed since) |
| Vehicles, first visit | 5 | 0 | 3 | 3, 0, 0, 0, 0 | 61.4 | 848 | FAIL (run 1 not preloaded; fixed since) |
| Alerts, first visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 17.4 | 947 | pass |
| Live, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 23.4 | 826 | pass |
| Dashboard, later visit | 5 | 0 | 1 | 0, 0, 0, 1, 0 | 25.1 | 1188 | pass |
| Reports, later visit | 5 | 0 | 1 | 1, 0, 0, 0, 1 | 25.9 | 947 | pass |
| Vehicles, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 17.4 | 847 | pass |
| Alerts, later visit | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 9.2 | 946 | pass |
| Dashboard, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 8.2 | 0 | pass |
| Vehicles, Reduce Motion | 5 | 0 | 0 | 0, 0, 0, 0, 0 | 8.0 | 0 | pass |
