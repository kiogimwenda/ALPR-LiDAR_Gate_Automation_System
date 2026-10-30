# Theme reveal (light ↔ dark) — measured 2026-10-30, mains power, final code

Release + harness build. Each wipe = pointer-down (`themepress`) then the tap (`theme`) ~90 ms
later, as a click on the top-right toggle; 5 runs per direction per layout, in-app frame traces
(`tools/motion-capture/theme_runs.sh`, `theme_analyse.py`). Tap → first wipe frame is measured
from the earlier of the handled tap and press + 90 ms (the tap can be handled late while the UI
thread restyles). Coverage = covered window area 1 % → 98.5 % (the calibration metric, §14), from
the wavy disc's logged radius and phase each frame (r·(1 + 0.06·sin(5θ + φ)), 48 points); the
next column is when the whole window is inside the disc. Pass: tap → first wipe ≤ 100 ms,
coverage 500 ± 33 ms, wipe median 0 / worst ≤ 1 dropped, no wipe frame over 33 ms.

## Wide (1500×960 px window, 985×601 dip; toggle top-right at 933,48)

| Direction | runs | tap → first wipe frame, median / worst (ms) | coverage: area 1 % → 98.5 % (ms, median, range) | whole window inside the disc (ms after first wipe frame) | wipe to overlay removed (ms) | dropped during the wipe, median / worst | longest wipe frame (ms) | longest frame under the static snapshot before the wipe (ms) | wipe frames | wave phase φ | origin (dip) | snapshot | result |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dark → light | 5 | 32.8 / 64.6 | 487 (487–492) | 597 | 622 | 0 / 0 | 7.9 | 110 | 149 | 0.00→2.51 | 933,48 in 985×601 | prepared | pass |
| light → dark | 5 | 30.7 / 90.6 | 488 (487–492) | 596 | 622 | 0 / 0 | 7.2 | 146 | 150 | 0.00→2.51 | 933,48 in 985×601 | prepared | pass |

## Phone layout (620×1000 px window, 399×627 dip; toggle top-right at 367,28)

| Direction | runs | tap → first wipe frame, median / worst (ms) | coverage: area 1 % → 98.5 % (ms, median, range) | whole window inside the disc (ms after first wipe frame) | wipe to overlay removed (ms) | dropped during the wipe, median / worst | longest wipe frame (ms) | longest frame under the static snapshot before the wipe (ms) | wipe frames | wave phase φ | origin (dip) | snapshot | result |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dark → light | 5 | 23.8 / 66.6 | 521 (521–521) | 596 | 622 | 0 / 0 | 7.5 | 108 | 150 | 0.00→2.51 | 367,28 in 399×627 | prepared | pass |
| light → dark | 5 | 28.2 / 45.0 | 521 (521–521) | 594 | 623 | 0 / 0 | 6.4 | 110 | 150 | 0.00→2.51 | 367,28 in 399×627 | prepared | pass |

"Longest frame under the static snapshot before the wipe" is the theme restyle (every
AppThemeBinding of every tab, then a layout and render pass). It now runs during the press,
under the old theme's snapshot, so nothing moves or changes on screen while it runs; it is
not part of the wipe.

## Edge cases (`theme_edge.sh`; end state from the harness "themeinfo")

```
== retap mid-wipe (to light, then to dark 250 ms in)
  themeinfo requested Dark user Dark busy False overlays 0 caption #060409
== tab switch mid-wipe (to light, Reports 250 ms in)
  themeinfo requested Light user Light busy False overlays 0 caption #E8E6E9
== resize mid-wipe (to dark, 1100x800 250 ms in)
  themeinfo requested Dark user Dark busy False overlays 0 caption #060409
== press held 1.4 s, then tap (to light)
  themeinfo requested Light user Light busy False overlays 0 caption #E8E6E9
== press without a tap (cancelled press)
  themeinfo requested Light user Light busy False overlays 0 caption #E8E6E9
== Reduce Motion (to dark)
  themeinfo requested Dark user Dark busy False overlays 0 caption #060409
```
Traces of the same: tap again mid-wipe → the running wipe ends at once (`theme.interrupted`) and a
new one starts from a fresh snapshot; tab switch mid-wipe → the wipe continues to the end, the
entrance runs under it; resize mid-wipe → the wipe is cut short (the snapshot is the old size);
press held 1.4 s → the press is undone at 1.0 s (`theme.cancel.stale`: old theme back under
the snapshot, snapshot dropped), and the tap then takes a fresh snapshot; press without a tap →
undone at 1.0 s; Reduce Motion → the snapshot cross-fades out in 200 ms (46 frames, 1.00 → 0.00).
In every case: no overlay left, no press or wipe still pending, the theme and the caption match
the last toggle.

## Snapshot and colours
Contact sheets, one wipe each way (Dashboard, wide): `theme-wipe-dark-to-light.jpg`,
`theme-wipe-light-to-dark.jpg`. The snapshot shows exactly the visible window. A first capture,
with the parked tabs drawn off-window, came out ~21000 dip wide and squeezed into a strip. Now
the tab host is clipped to its bounds while a reveal runs, and the parked tabs are dropped from
rendering during it. The new theme is never visible before the wipe except in the window
caption: the caption is outside the snapshot, and switches on the wipe's first frame (it used to
switch the moment the theme was applied). After the wipe, the pill, cards, glow, chart and
caption are all in the new theme.
