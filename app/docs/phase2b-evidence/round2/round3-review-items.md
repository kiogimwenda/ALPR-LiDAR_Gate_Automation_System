# Review round 3 (interim verdict) — items 0–5

**0. Harness build.** Every acceptance run used Release with the harness compiled in
(`-p:GcHarness=true`). This is stated at the top of `acceptance-table.md`. The shipped Release
binary contains no harness (`builds/release-harness-check.txt`).

**1. Keyboard focus into parked tabs: fixed.**
- **The problem:** a focus walk from the Dashboard landed on controls in the parked Live and
  Reports tabs, the Vehicles search box and the Reports date picker among them. Before the fix,
  the UIA dump with the Dashboard active also listed the Live tab's Open / Close buttons,
  20000 dip off-window (`a11y/uia-before-fix-dashboard.txt`).
- **Fix:** each hosted tab is wrapped in `Controls/ParkHost.cs`. While the tab is parked:
  - its native controls are not tab stops (`IsTabStop = false`, restored on unpark). They are
    found through the MAUI tree, at idle. A native visual-tree walk took about 80 ms on Reports
    and dropped frames.
  - focus already inside the tab moves out when it is parked.
  - a `GettingFocus` check cancels any remaining move into it, using `TryCancel`. Setting
    `Cancel` threw for activation focus changes. Redirecting focus from inside `GettingFocus`
    crashed WinUI in `TryMoveFocusAsync`.
  - It does not use IsEnabled.
- **Proof:** the harness `tabstops` command lists every control that Tab or the arrow keys
  could reach (IsTabStop, enabled, visible). With each tab active, the count inside parked tabs
  is **0** (`a11y/summary.txt`).
- **Pre-existing, outside this item:** most of the app's tappable elements are custom views,
  not native controls, so the reachable count is small: Live 6, Dashboard 1, Reports 12,
  Vehicles 3, Alerts 1.

**2. Screen readers: fixed.**
- The parked panel's automation peer reports no children, and the wrapper itself is not a
  control element.
- UIA control-view dumps with each tab active list **0 elements outside the window**
  (`a11y/uia-tab0..4.txt`). Before the fix, the Dashboard dump listed the whole Live tab.
- A Narrator pass was not done, as it needs a person at the machine. The dump shows the same
  tree Narrator's scan mode walks.

**3. WinTransform containers: fixed.**
- When a view has a container (WrapperView), `Apply` now removes our transform from the inner
  view if it is ours.
- **Test** (harness `parity` + `parityclip`, `parity/`): a box moved to TranslationY 30, then
  given a Clip, then moved to (40, 15).
  - Our path puts it at the requested (40, 15).
  - Stock MAUI leaves it lower: it keeps part of the earlier offset once the container appears
    (`parity/compare-2.jpg`, top = ours, bottom = stock MAUI).
  - The final positions therefore differ by design. Ours is the requested one.

**4. Semantics parity** (same page, both paths, `parity/ours-1.png` vs `parity/maui-1.png`):
- Covered: AnchorX/Y (0,0), (0.5,0.5) and (1,1) with Scale 0.96 and TranslationY 8, written
  through `Spr`; a box under a FlowDirection=RightToLeft parent moved by TranslationX 20; and a
  Reduce Motion-style instant `Spr.Set` (Scale 0.9, TranslationY 12).
- **Result:** 90 pixels differ, all in one 1-px antialiasing column at the RTL box's edge.
  Positions, anchors, scale and the RTL TranslationX sign are identical. MAUI applies no RTL
  flip to translations, and neither do we.
- Reduce Motion writes go through the same mapper (`Spr.Set` → property → `WinTransform`) with
  no spring state.

**5. SettleXamlCleanup robustness: re-armed.**
- **Re-arm:** a host resize, which includes maximise / restore, re-runs it 600 ms after the last
  size change, at idle. A theme change re-runs it after the parked tabs come back.
- **Single runner:** a newer run stops an older one, and the nudge skips a tab that has become
  current.
- **Check** (`tools/motion-capture/settle_check.sh`, 3 rounds, before this round's last ParkHost
  changes): resize → enter a tab, maximise / restore → enter, theme change → enter, plus a tab
  switch 0.75 s after a resize (during the nudge).
  - No gen-2 collection in any entrance, and no frame over 27 ms.
  - Three entrances had one 26–27 ms first frame: right after a resize, a max/restore, and a
    theme change.
- **Not measured:** low-end GPUs.

**6 (optional), not done:** driving the ticker from Composition.

**Final review, item 4 (group 7):** the low reachable counts above were a Phase 2a regression,
caused by custom tappables that were not focusable. With group 7, `Pressable` makes every
tappable a keyboard stop. Reachable controls per tab: Live 20, Dashboard 17, Reports 80,
Vehicles 18, Alerts 17, with 0 inside parked tabs (`a11y-after/`).
