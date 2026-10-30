# Design spec from the reference video ("Dashboard V14")

Source: `C:\Users\User\Videos\WhatsApp Video 2026-10-19 at 13.25.09.mp4`
(WSL: `/mnt/c/Users/User/Videos/WhatsApp Video 2026-10-19 at 13.25.09.mp4`).
What the video shows, measured, and how it maps onto our MAUI app.
**Revision 2 (Phase 2a):** reviewer changes 1–10 and the four decisions
are applied (§9 A5/A6/A11/A12, §4, §5, §8, §10, §11, §13); §14 records
what Phase 2a built and measured.

Reference frames are in [`design-video-frames/`](design-video-frames/)
(cropped to the UI, 2× upscaled then reduced; file names carry the source
frame number `fNNN`, t = (N−1)/30 s).

## 1. Video facts

| | |
|---|---|
| Container | MP4 (WhatsApp re-encode), H.264 + AAC |
| Duration | 5.93 s, 178 frames |
| Frame rate | 30 fps constant (one frame = 33.3 ms; timings below are ±17 ms) |
| Resolution | 576 × 1024 portrait |
| Content | A social-media "design showcase" clip: title card "Dashboard V14" (top), a **desktop web dashboard** ("Vertix", a fintech admin) recorded at ~525 px wide in the band y = 354–668, and a code panel below showing React + `framer-motion` + `lucide-react` (`<motion.div whileHover={{ y: -5 }} className="glass-card p-6 rounded-2xl">`). |
| Edits | Hard cuts at f48, f79, f88, f100 (one-frame jumps); the clip is a montage, so some transitions start off-camera. |

Because the UI is only ~525 px wide in the recording (a ~1440 px layout
downscaled ≈ 2.7×), small text is not legible and pixel measurements carry
±1 video px (≈ ±3 design px). Values below are converted to design px /
MAUI device-independent units (dip) with that factor and rounded to the
4-pt grid.

Timeline:

| Frames | Time | What happens |
|---|---|---|
| f1–f47 | 0.00–1.53 s | **Overview** page first load: staggered blur-fade build, number count-ups, bar chart growing. Cursor clicks the sidebar "messages" icon at ~f45. |
| f48–f63 | 1.57–2.07 s | Cut. Shell only (nav, rail), content area empty; top-nav pill gone (route not in the top nav); rail selection moved to the mail icon. |
| f64–f78 | 2.10–2.57 s | **Messages** page builds in: title, chat pane, then conversation list rows staggered. |
| f79–f87 | 2.60–2.87 s | Cut. Empty shell again; rail selection on another icon. |
| f88–f99 | 2.90–3.27 s | **Manage** page (already mostly in): card-controls rows and slider card stagger in. |
| f100–f150 | 3.30–4.97 s | Click "Program" in top nav: **pill slides** Manage → Program; **Rewards Program** page builds in with staggered cards and a large count-up (0 → 12,406 → 12,480). |
| f151–f171 | 5.00–5.67 s | Click the moon in the rail: **theme reveal** — dark theme spreads from the button as an organic, wavy-edged circle and covers the window in ~0.56 s. |
| f172–f178 | 5.70–5.93 s | Dark theme, static. |

## 2. Screens seen

1. **Overview** (`06-overview-settled-f047.jpg`): greeting title "Good
   morning, Code XR" + subtitle; row 1 = Total Balance card (big number,
   +% chip, Transfer (black capsule) / Request (grey capsule) buttons, a
   recessed "Wallets" well with three currency tiles) · a 2×2 grid of stat
   tiles in one container (first tile is the **accent tile**: orange
   gradient, white text) · Total Income card with a stacked bar chart
   (dark "Loss" bars + orange "Profit" caps, month labels, legend dots).
   Row 2 = Monthly Spending Limit (progress bar) · Recent Activities table
   (search, filter, header row on a grey well, rows with status dot).
2. **Messages** (`08-messages-list-stagger-f078.jpg`): two-pane — left a
   search field + conversation list (avatar, name, preview, unread dot),
   right a conversation (header with avatar/online, bubbles on grey,
   composer with an orange round send button).
3. **Manage** (`09-manage-settled-f099.jpg`): title + subtitle + a black
   "+ New card" capsule at the right; left card "Your cards" (a dark
   payment card on a grey well, card chips, four square action tiles
   Freeze / Show number / Copy / Flip); right card "Card controls" (icon,
   title, caption, **orange toggle** rows; "Active" green chip) and a
   limit slider card.
4. **Rewards Program** (`13-program-settled-light-f150.jpg`, dark
   `16-program-settled-dark-f178.jpg`): a wide **hero accent card**
   (orange radial gradient, "Gold member" chip, 64-px count-up number +
   "points", thin white progress track with hatched remainder, white
   "Redeem points" capsule) · Tier benefits card (mini segmented control
   Silver/Gold/Platinum, check list) · Offers card (three tiles, selected
   one tinted orange with orange outline, black "Activate" capsules) ·
   Points history list (round icon, title, date, signed amount in
   green/red).

Shell around every screen (persistent, never animates on page change):

* **Window canvas**: page sits in a large rounded panel (radius ≈ 28)
  inset ~12 dip from the window edge on a slightly darker outer colour.
* **Top bar** (three floating white capsules on the canvas): brand (round
  orange logo + "Vertix") · **segmented nav capsule** with six text items
  and a black sliding pill behind the selected one · icon capsule (search,
  bell with orange dot, info) · profile chip (avatar, name, email,
  chevron).
* **Left rail**: three separate floating white capsules, 36 dip wide —
  theme (sun / moon), main icons (7; the selected one is a filled black
  circle with a white glyph), bottom (help, sign out).

## 3. Layout grid and spacing

Measured in design px (≈ dip):

| Token | Value | Where |
|---|---|---|
| Window inset | 12 | outer colour → canvas panel |
| Canvas padding | 24 (top 16) | panel → top bar / rail |
| Rail width | 36 (capsules), gap 16 to content | |
| Top bar height | 40 capsules, 16 below to title | |
| Title block | 32 title, 4 gap, 13 subtitle, 24 to first row | |
| Gutter | 16 between cards (both axes) | |
| Card padding | 16 (20 on hero) | |
| Inner well padding | 12, inner tiles gap 8 | |
| Column grid | 12-col; Overview 4/5/3, Program 7/5, Manage 6/6, Messages 4/8 | |

Rhythm is a strict 4-pt grid; nothing is closer than 8 between unrelated
items.

## 4. Typography

A neo-grotesk with tight tracking, very close to **Inter** (single family,
weights 400/500/600/700; tabular figures on money).

**Decision:** Inter 4.1 is bundled (OFL) as **"Inter GC"**: MAUI cannot
switch on the OpenType `tnum` feature, so the four static weights are
rebuilt with `tnum` frozen into the character map (`tools/design-assets/
freeze_tnum.py`: every digit 0–9 gets the same advance, e.g. 1328 units in
Regular, against 833–1323 before) and the family renamed, as the OFL
requires of a modified font. All text therefore has tabular figures, which
also suits plates and tables. The platform font is only a fallback.

| Role | Size / weight / tracking | Example |
|---|---|---|
| Page title | 32 / 600 / −0.02 em | "Rewards Program" |
| Hero number | 56–64 / 600 / −0.03 em, tabular | "12,480" + 15/500 "points" |
| Card number | 26–28 / 700 / −0.02 em, tabular | "$635,386.60", "$872" |
| Card title | 15 / 600 | "Tier benefits" |
| Body / row title | 13 / 500 | "Flight Ticket Booking" |
| Secondary / caption | 11–12 / 400, secondary ink | "15 Apr", subtitles |
| Nav item | 13 / 500 (selected: same weight, inverse colour) | |
| Chip | 10–11 / 600 | "+3%", "2 active", "Gold member" |

## 5. Colour palette

Sampled with PIL from frames f047/f150 (light) and f178 (dark): median of
the relevant pixel class over a region; the encode shifts colours by a few
units, values are rounded to sensible tokens.

| Token | Light | Dark | Notes |
|---|---|---|---|
| `Outer` (window behind canvas) | `#E8E6E9` | `#060409` | |
| `Canvas` (page panel) | `#F3F1F5` | `#110E15` | slightly violet-tinted neutrals |
| `Surface` (cards, capsules) | `#FFFFFF` | `#17151A` | |
| `Well` (recessed area inside a card) | `#F3F1F5` | `#1F1C22` | Wallets, table header, offer tiles |
| `Ink` (primary text, selected pill, primary button) | `#151316` | `#F4F2F7` | inverts: black pill → white pill in dark |
| `OnInk` | `#FFFFFF` | `#151316` | |
| `SecondaryInk` | `#8B888F` | `#8F8C94` | captions |
| `Hairline` | `#ECEAEE` | `#26232A` | table/list dividers |
| `Accent` (orange) | `#E15A41` | `#E15B43` | same in both themes |
| `AccentGradient` | `#F2947F` (top-left highlight) → `#E15A41` → `#D2432A` (bottom-right) | same | radial highlight at ~70 % x, 30 % y |
| `AccentTint` (chips, selected offer) | `#FBE3DC` | `#3A1F1A` | measured `#F7D0C6` / `#521D18` under a chip |
| `Positive` | `#1E9E5A` (text), tint `#E3F4EA` | `#3CCB7F`, tint `#173326` | "+3%", "+1,206", "Active" |
| `Negative` | `#E5484D` | `#F2555A` | "−2,500", "−5%" |
| Bar series | dark bar = `Ink`, cap = `Accent` | dark bar = `#F4F2F7`?* | *the dark-theme chart wasn't shown; use `Ink` (inverts) |

**Decision (accent):** orange (`Brand`) only for the brand mark, the
accent tile / hero card, toggles, send buttons, unread / notification
dots and the selected-tile tint. **Ink** for selection, the nav pill, rail
selection, primary buttons, links and focus. iOS blue is gone everywhere:
`grep -rniE "007AFF|0A84FF|0078D4|Colors.Blue"` over `app/src` finds
nothing, and the WinUI system accent is overridden to ink
(`Platforms/Windows/App.xaml`) so focus bars and native controls are not
blue either. Caution is `#F5A524` (distinct from the brand orange). The
token key `Accent` keeps its meaning "interactive/selection" and is now
ink; data drawn in the accent tone (e.g. "On site now", "Opening") uses
`Brand`, because converter-coloured text is not re-evaluated on a theme
switch and ink would vanish on dark cards.

## 6. Shape, depth and materials

* Radii: canvas 28 · card 20 · inner tile / well 14 · stat tile 16 ·
  hero card 20 · payment card 14 · chips and buttons fully rounded
  (capsules) · avatar circles.
* Shadows: almost none. Cards are flat white on a tinted canvas; the
  accent tile carries a soft coloured glow (orange, blur ≈ 24, y 8,
  opacity ≈ 0.25); the payment card a neutral drop shadow (blur 24, y 12,
  opacity 0.25). The selected offer tile has a 1-dip accent outline.
* No glass/translucency is visible in the UI (the code's `glass-card`
  class renders as solid). Our "one translucent layer" rule stays (sheet
  scrim only).
* Hatching: the remaining part of the hero progress bar is diagonally
  hatched white @ 25 %.

## 7. Icons

Lucide (outline, 1.5–2 px stroke, rounded caps), 16 dip in rails and
buttons, 14 in chips. Selected rail icon: white glyph on a 28-dip black
circle. We will ship the few we need as SVGs in `Resources/Images`
(Lucide is ISC-licensed): `layout-grid`, `activity`, `file-text`, `car`,
`bell`, `search`, `sun`, `moon`, `users`, `door-open` (gates),
`database`, `log-out`, `settings`, `plus`, `share`, `chevron-down`,
`check`, `x`.

## 8. Components

| Component | Spec |
|---|---|
| **Nav capsule with sliding pill** | White capsule, height 40, padding 4; items 13/500 ink with 16-dip horizontal padding, **content-width** (measured ≈ 68 / 59 / 61 dip for the video's items); one black (light) / white (dark) pill behind the selected item that follows both position and width; selected label switches to `OnInk`. When the current page is not a nav item, the pill fades out (f48). |
| **Rail** | Capsules of icon buttons 28 dip, 8 apart; selection = filled `Ink` circle. |
| **Card** | `Surface`, radius 20, padding 16, no border, no shadow; header row = 15/600 title + optional right accessory (chip, "2 cards", search, filter). |
| **Well** | `Well` colour, radius 14, inside a card (table header, tiles container). |
| **Stat tile** | Inside a container card: caption + round icon (top-right, 20 dip, `Well`), 28/700 number, a ±% chip + "This month" caption. |
| **Accent tile / hero card** | `AccentGradient`, white text, white @ 20 % chips, coloured glow. |
| **Buttons** | Primary = `Ink` capsule with `OnInk` text, height 36, icon + label; secondary = `Well` capsule; on-accent = white capsule with `Ink` text; destructive keeps `Negative`. |
| **Chips** | Capsule, 10–11/600, tint background + tone text (`+3%`, `Active`, `2 active`). |
| **Toggle** | Accent-orange track when on, `Well` when off, white knob. |
| **Segmented (mini)** | `Well` capsule with a white thumb (Tier benefits) — our `SegmentedControl` restyled. |
| **List row** | 40-dip leading round icon (`AccentTint` or `Well`), title 13/500, caption 11, trailing value right-aligned (signed, toned). Hairline separators or none. |
| **Table** | Header row on a `Well` capsule, 11/500 secondary; rows 13, status dot + text. |
| **Bar chart** | Rounded-top bars (radius 4), width ≈ 60 % of slot, stacked series (dark base + accent cap), light grid lines, axis labels 10 secondary, legend dots top-right. |
| **Progress** | 6-dip capsule track; fill accent (on cards) or white (on the hero). |
| **Search field** | `Well` capsule, leading search icon, 13 placeholder. |

## 9. Animation catalogue

Spring parameters are in our model (`SpringSpec(DampingRatio, Response)`,
`GateControl.Core/Motion/Spring.cs`). Fits were done in Python against
per-frame measurements (pixel tracking / region convergence to the final
frame); "err" is the sum of squared residuals of normalised progress.

| # | Name | Frames (time) | Trigger | Properties | Measured | Model | Stagger | Ref frames |
|---|---|---|---|---|---|---|---|---|
| A1 | **Shell build-in** | f1–f20 (0–0.63 s) | first load | header capsules, logo text, rail capsules: opacity 0→1 + blur→0 (rail icons fade top→bottom) | header 90 % at f29 (~0.9 s, includes count-ups); capsules visible by f8 | opacity spring (1.0, 0.4); rail icon stagger 0.05 s | header → rail → title | 01, 02 |
| A2 | **Title word reveal** | f8–f23 (0.23–0.73 s); Program f100–f110 | page enter | each word: opacity 0→1, blur ≈ 8 px→0, slight rise | per word ≈ 0.3 s, words 3 frames (0.10 s) apart | opacity + TranslationY 6→0, spring (1.0, 0.4) | 0.08–0.10 s per word | 02, 03, 11 |
| A3 | **Card stagger enter** | Overview f11–f30; Program f100–f135 | page enter | card: opacity 0→1, blur→0, scale 0.96→1, translateY +4→0 (design px ≈ +10) | hero card: fade fit **(1.0, 0.40)** err 0.004, t0 = cut; scale 258→269 px wide (0.96→1) fit (1.0, 0.20); cards start at hero 0.00 s, tier +0.20 s, history +0.27 s, offers +0.57 s (≈ 0.12–0.15 s per card in reading order) | opacity/scale/Y spring **(1.0, 0.4)** = `SpringSpec.Move` | **0.12 s** per card, reading order (row-major) | 11, 19 |
| A4 | **Card content stagger** | Overview f20–f40; Manage f88–f99 | after its card | rows/inner tiles: opacity + blur, slight rise | toggle rows start 2 frames apart (0.067 s), each ~0.25 s | opacity + Y 8→0, (1.0, 0.35) | **0.065 s** per row | 03, 09 |
| A5 | **Number count-up** | Overview f20–f47+ (cut before settle); Program f108–f149 | value appears / changes | the number's text value (not a transform) | lands **exactly** on 12,406 at f149 and holds it f149–f158 (12,480 at f161 is a later data update, not the same count); **ease-out-cubic over 1.45 s** from f108.1 fits 15× better than any spring (table below) | **timed** `TimedProgress.CountUp()` (OutCubic 1.45 s) — 2nd documented exception; a new value mid-count restarts the curve from the value on screen | balance, tiles start with their card | 03, 04, 12 |
| A6 | **Accent tile reveal** | f25–f31 (0.80–1.00 s) | card enter | orange fill grows from the top-left corner as a clipped rounded blob, then sharpens; the text is never scaled | 90 % in 6 frames (0.2 s) | `BlobClip.GrowIn` (same wavy-disc clip as A11) driven by a (1.0, 0.3) spring — 90 % in 0.19 s; Reduce Motion → fade | after its container | 04 |
| A7 | **Bar chart grow** | f26–f47 (0.83–1.53 s) | chart enter / data | each bar height from 0 (anchored at baseline); stacked cap grows after the base | bar n starts 2 frames after bar n−1 (**0.065 s**); base ≈ 0.25 s, cap starts ≈ 0.25 s after base; whole chart ≈ 0.7 s | per-bar progress spring (1.0, 0.45), cap delayed 0.2 s | 0.065 s left→right | 04, 05, 19 |
| A8 | **Nav pill slide** | f99–f110 (3.27–3.63 s) | tap a nav item | pill translateX (37 video px = item pitch); width constant (adapts to item width); labels cross-fade ink ↔ on-ink | 5, 14, 21, 26, 30, 32, 34, 35, 36, 37 px per frame; fit **(1.0, 0.36)** err 0.002 — no overshoot | `Spr.To(TranslationX, SpringSpec.Default)` (1.0, 0.35) + width spring (scale-free: animate a second inner view, see §11) | — | 10, 17 |
| A9 | **Page content swap** | f99→f100 (one frame) | nav selection | old page content removed instantly (no exit), new page builds in with A2/A3/A4 | 1-frame cut; first new frame shows content at ~30 % opacity, blurred | old: Set opacity 0; new: A2–A4 | — | 09, 10, 11 |
| A10 | **Two-pane list stagger** | f64–f78 (Messages) | page enter | pane fades, then list rows opacity + slight rise | rows ≈ 0.07 s apart | as A4 | 0.065 s | 07, 08 |
| A11 | **Theme reveal** | f152–f171 (5.03–5.67 s) | tap sun/moon | new theme spreads from the button centre as a circle with a wavy (blob) edge; at full cover the change is complete | radius (video px): 22, 27, 34, 45, 58, 76, 100, 130, 166, 205, 248, 271, 348, 400, 447, 484, 493, 504, 509 → **ease-in, quadratic**: r = R·(t/0.56 s)² fits err 0.007 (springs fit 10× worse: a spring decelerates, this accelerates); starts at button size (≈ 4 % of R) | timed **0.56 s ease-in-quad** (`TimedProgress.ThemeReveal()`, 1st exception) on a **wavy** disc: closed 48-point polygon r·(1 + 0.06·sin(5θ + φ(t))), φ drifting 0.8π over the wipe; snapshot + clip (§11) | — | 14, 15, 18 |
| A12 | **Hover lift** | (from the on-screen code, not filmed) | pointer over a card (desktop) | translateY 0→−5 | n/a | **−5** dip, (1.0, 0.35), pointer platforms only; touch keeps Pressable | — | code panel |
| A13 | **Rail selection** | f47→f48, f78→f79 (hidden by cuts) | tap a rail icon | black circle moves to the new icon | not measurable | same as A8 vertically | — | 06, 07 |

General character: **critically damped everywhere** (no visible
overshoot in any measured motion), short responses (0.35–0.45 s) for
layout, long (≈ 1.5 s) for numbers, and stagger as the main source of
richness. Blur-in (CSS `filter: blur`) is used heavily on enter; we
reproduce it where the GPU can (Android 12+) and substitute elsewhere
(§11, deviations).

A5 fit (hero count-up, final 12,406; frames read from the video):

| frame | f114 | f118 | f124 | f132 | f135 | f140 | f145 | f149 | f149–f158 |
|---|---|---|---|---|---|---|---|---|---|
| shown | 4,360 | 6,725 | 9,255 | 11,210 | 11,756 | 12,166 | 12,371 | 12,406 | 12,406 (holds) |
| OutCubic 1.45 s from f108.1 | 4,394 | 6,689 | 9,237 | 11,271 | 11,717 | 12,171 | 12,363 | 12,403 | 12,406 |

Sum of squared normalised residuals: OutCubic **7×10⁻⁵**; best critically
damped spring 1.1×10⁻³ (15× worse, and a spring creeps instead of
landing). Encoded as xUnit test `CountUpMatchesTheReferenceVideo`
(±1 % of the final value).

## 10. Mapping onto Gate Control

Our navigation becomes the video's shell: **top nav capsule with sliding
pill** for the five tabs (Live, Dashboard, Reports, Vehicles, Alerts) on
wide windows (≥ 900 dip: Windows, Mac, tablets landscape), and the **same
capsule floating at the bottom** on phones (five icon+label items, pill
behind the selected one). Rail (wide only): theme toggle capsule, an
admin capsule (Users, Gates, Data — admins only), and a bottom capsule
(Account, Sign out). On phones the theme toggle and account live in the
header (brand · bell · avatar). The profile chip opens Account (Settings
page). The bell carries the alerts count as an orange dot/badge
(replacing "Alerts (n)" in the tab title).

| Our page | Video pattern | Changes |
|---|---|---|
| **Sign in** | Canvas + one centred card | Brand logo (round accent), title 32/600, `Well` capsule fields, primary `Ink` capsule button; card A3 enter, fields A4 stagger. |
| **Live** | **Rewards Program** (hero + side card + tiles + history) | Hero accent card = today's tally (count-up allowed / refused / on site, A5) with the feed state chip ("Live"); gate tiles = Manage-style "controls" cards (state chip, beam/link rows, Open/Close as `Ink`/`Well` capsules, ••• menu); "Latest vehicles" = Points-history list (plate, gate · time caption, verdict chip right). New rows keep `Spr.Enter` drop-in (spatially consistent with the feed). |
| **Dashboard** | **Overview** | Greeting title ("Good morning, {name}") + subtitle with period; period `SegmentedControl` as a mini capsule (Tier benefits style) at title right; KPI tiles in a container card (2×2 / 4×2) with the **first tile as the accent tile** (Vehicles today); count-ups on all KPIs; "Vehicles by hour" and "Entries vs exits" as **stacked bars** (allowed = `Ink`, refused = accent cap) with A7; donut/horizontal bars restyled with ink/accent; "Most frequent" as the Recent Activities table. |
| **Reports** | Overview table + Messages two-pane | Search filters in a card (search capsule, filter chips); results as the Recent Activities table (header well, rows, status dot); on wide windows selecting a row opens the plate history in a right pane (Messages pattern) instead of pushing; Visits / Unreadable / Trend sub-tabs use the nav-capsule style at card level. |
| **Vehicles** | **Manage** | Title + subtitle + "+ Add vehicle" `Ink` capsule at title right; list card with search capsule; rows: round icon, plate 13/600, owner · unit caption, expiry chip; edit sheet keeps `Sheet` (restyled: radius 20, `Surface`, capsule fields, accent toggles for allowed types). Swipe-to-remove unchanged. |
| **Alerts** | **Messages** two-pane | Left: list with tone-tinted round icons, unread dot, "Clear all" capsule; right (wide): alert detail with plate history link and Clear button. Phone: list only, detail pushes. |
| **Plate history** | Program "Points history" + hero | Hero = a `Surface` card (not the orange accent card) with the plate in large type, a registered / blocklisted chip, visits as a count-up; the decisions below as the Points-history list. |
| **Account / Settings** | Manage "Card controls" | Settings rows with round icons and accent toggles; appearance = mini segmented (Auto / Light / Dark) — choosing Light/Dark plays A11. |
| **Users, Gates, Data** (admin) | Manage | Cards with control rows, `Ink` primary capsule for add/save. |

**No demo content from the video ships.** "Vertix", "Code XR", the
greeting "Good morning, Code XR", currency figures and card numbers are
reference only. The Dashboard greeting is built from the signed-in user's
display name and the local time of day ("Good morning / afternoon /
evening, {name}"); the brand is "Gate Control" with our own mark.

## 11. Motion primitives: what exists, what is needed

Existing (`Motion/Motion.cs`, `Controls/Sheet.cs`, `Core/Motion/Spring.cs`):

| Need | Covered by |
|---|---|
| Press feedback on every button / pressable | `Pressable` (scale 0.97, Press spring) — keep |
| Interruptible springs on transform/opacity | `Spr.To` / `Spr.Set` — keep; all specs below are expressed with it |
| Sliding thumb | `SegmentedControl.MoveThumb` (TranslationX, `Move`) — reuse its logic for the nav pill |
| Item drop-in | `Spr.Enter` — keep for live feed rows |
| Sheets | `Sheet` — keep, restyle only |
| Chart data morph | `ChartView` single `_progress` spring — extend |

New primitives (built in Phase 2a; file → what it does):

1. **Core `Motion/Timing.cs`** (pure, unit-tested): `TimedProgress` with
   `Curve.OutCubic` / `Curve.InQuad` (retarget restarts from the value on
   screen; `CountUp()` = OutCubic 1.45 s, `ThemeReveal()` = InQuad 0.56 s);
   `Stagger` (cards 0.12 s, rows/bars 0.065 s, words 0.09 s, capped at 12
   items); `RevealSpec` (rise, scale, blur radius and blur-out curve);
   `Blob` (48-point wavy outline, 5 lobes, ±6 %, cover radius, phase
   drift). `Formatting/NumberText.cs` finds the number inside a display
   string for count-ups (keeps prefix/suffix, decimals, grouping, culture;
   clock times and dates are never counted).
2. **Ticker** (`Motion/Motion.cs`): per-track start delay (stagger),
   `Prop.ScaleX`, and `Spr.OnFrame` per-frame callbacks for motion that
   isn't a spring on a view property; `MotionSettings.Override` (DEBUG
   harness only).
3. **`Reveal`** (`Motion/Reveal.cs`): attached `m:Reveal.Order` /
   `Delay` / `Kind` (Card, Row, Word) and `Reveal.Page(root)`; untagged
   pages get their top-level items revealed as cards 0.12 s apart.
   `PageBase` runs it for pushed pages; the root runs it on every tab
   switch (A9). `Controls/TitleLabel.cs` splits a title into words for A2.
4. **`PlatformBlur`** (`Motion/PlatformBlur.cs`): Android 12+ blur via
   `RenderEffect.CreateBlurEffect`, stepped per frame with
   `RevealSpec.BlurAt` (8 dip → 0 over 0.25 s, ease-out) alongside the
   opacity spring; a global `Enabled` switch to fall back if a device drops
   frames.
5. **`CountUp`** (`Motion/CountUp.cs`): attached `m:CountUp.Text` on a
   Label (and `m:CountUp.Key` so a rebuilt item template continues from
   the last value instead of recounting from zero on every 30-s refresh).
6. **`BlobClip`** (`Motion/BlobClip.cs`): wavy-disc clip geometry (disc;
   window-minus-disc with the disc wound the other way, so the hole is cut
   under either fill rule — WinUI ignores even-odd) and `GrowIn` for A6.
7. **`ThemeReveal`** (`Motion/ThemeReveal.cs`, decision: snapshot + clip):
   `CaptureAsync` the window → overlay `Image` → switch `UserAppTheme`
   underneath → per frame, clip the snapshot to window-minus-wavy-disc
   along InQuad 0.56 s → remove. Capture failure → logged overlay-scale
   fallback. Reduce Motion → 0.2 s cross-fade. A second tap finishes the
   running wipe at once and starts the new one.
8. **`HoverLift`** (`Motion/HoverLift.cs`): −5 dip on pointer enter.
9. **Shell** (`Pages/RootPage.cs`, `Controls/NavCapsule.cs`,
   `Controls/RailCapsule.cs`, `Controls/IconView.cs` + generated
   `Controls/Icons.cs`): see §14.

### Deviations from the reference, per platform

| Item | Windows | Android 12+ | Android < 12 | iOS / Mac |
|---|---|---|---|---|
| Reveal blur (A1–A4, A10) | substitute: opacity + 8-dip rise + 0.96 scale (no scale on cards taller than 360 dip or holding a chart) | blur 8→0 dip (RenderEffect) when enabled; disabled by default (see below) | substitute | substitute |
| Theme reveal (A11) | snapshot + animated clip (verified) | snapshot + clip (built, not run-tested; needs a device) | same | same (not built here) |
| Theme reveal curve | **0.62 s, InQuad on covered area** (`TimedProgress.ThemeReveal`): a timed curve, not a spring, calibrated so the covered area grows like the reference's wipe | same | same | same |
| Theme reveal start | first wipe frame **median 24–33 ms, worst 91 ms** after the tap (round 3: snapshot *and* theme switch on pointer-down, under the snapshot; the tap only starts the wipe; keyboard / screen-reader activation has no press and takes ~100–140 ms) | native PixelCopy snapshot (API 26+), View.Draw fallback (built, not run-tested; needs a device) | — | PNG round-trip (not tuned) |
| Window caption during the wipe | outside the snapshot, so it cannot be wiped: it switches on the wipe's first frame (it used to switch when the theme was applied) | — | — | — |
| Theme toggle position | **top-right of the window** (user request), wipe from there; the reference has it on the rail | same | same | same |
| Accent glow | **restored as a pre-rendered image** (`accent_glow.png`, a blurred brand rounded rect) behind the accent card, faded in by opacity once the blob clip has settled; no MAUI `Shadow` (that cost 18–25 ms frames and its WrapperView threw when toggled). Frames during the fade are no longer than the frames before it (`phase2b-evidence/round2/glow-frames.md`) | same | same | same |
| Reveal blur | — | **off by default** (`PlatformBlur.Enabled = false`) until measured on a device: the emulator did not boot | — | — |
| Animated non-transform properties | count-up text; one clip on one overlay (A11) or one card (A6); nothing in lists | + RenderEffect | — | — |
| First visit of a tab | tabs pre-laid-out, pre-loaded (≤ 2 s wait each), rendered once off screen, their transforms created, and WinUI's memory check run at idle (§17); no forced collection; every entrance 0 dropped frames (§17) | not measured (no device) | | |
| Parked tabs | opacity 1 but 20000 dip outside the window (dropped to opacity 0 during a theme reveal), wrapped in `ParkHost`: not tab stops, focus moves into them cancelled, no UI Automation children (round 3); nothing drawn inside the window (no ghosting, `round2/parked-on-settled-all.jpg`), render data kept | input-transparent + ExcludedWithChildren only (not run-tested) | same | same |
| Animated transforms / A6 clip / chart text (Windows) | one WinUI transform per element updated in place (`WinTransform`); A6 blob as one composition path whose clip scales and turns; chart text on the Win2D session with cached formats — no WinUI objects created per frame | MAUI defaults | MAUI defaults | MAUI defaults |
| Long lists in a revealed card | rows past the first 12 sit at opacity 0 during the card's fade and come back 6 per frame after it settles (they are below the fold) | same | same | same |
| Accent-tone data colour | `Brand` instead of ink (see §5) | same | same | same |

Platform constraints (unchanged): no animated `WidthRequest`, `Margin`,
`CornerRadius`, colours or shadows; colour changes snap or cross-fade via
two stacked views; large `Shadow` only on the accent tile / hero.
Reduce Motion is honoured by every new primitive (verified on Windows
through the DEBUG override, §14).

## 12. Phase 2 implementation plan

**Status after 2a:** steps 1–4, 6, 7 and 9 done (step 6 as a persistent
root page, not per-page templates; step 9's Lucide icons replace the tab
icons in the shell). Remaining for 2b: step 5 (ChartView), step 8 (pages),
and step 10 on Android.

Each step builds on Windows and keeps all features; check screenshots
against the reference frames after steps 3, 6, 8 and 10.

1. **Tokens**: rewrite `Colors.xaml` with §5 (keep key names where
   semantics match: `Background*`→canvas, `Surface*`, add `Outer*`,
   `Well*`, `Ink*`, `OnInk*`, `AccentTint*`, `Hairline*`; Accent = orange).
   Bundle Inter (OFL) in `Resources/Fonts` and register in `MauiProgram`;
   update `Styles.xaml` type ramp (§4), `Card` (radius 20, no shadow),
   new `Well`, `AccentCard`, `Chip`, capsule button styles, toggle colours.
2. **Spring additions (Core, unit-tested)**: `SpringSpec.Count`,
   `Easing.InQuad`-style `TimedProgress` for A11 only, a pure
   `Stagger.Delay(i, step)` helper; xUnit tests for the new math.
3. **Ticker delays + `Spr.Reveal` + `Stagger`** in `Motion.cs`;
   `Reveal.Order` attached property; `PageBase` entrance uses it (Reduce
   Motion path tested by toggling Windows animations).
4. **`CountUpLabel`**; wire to Dashboard KPIs, Live tallies, plate visits.
5. **`ChartView`**: stacked bars, per-bar stagger, new colours, rounded
   tops, legend.
6. **Shell**: `NavCapsule` (+ rail variant) in a `ControlTemplate` applied
   to the five tab pages; hide the Shell tab bar/nav bar on them
   (`Shell.TabBarIsVisible=False`, `Shell.NavBarIsVisible=False`); the
   template's pill starts from the last selected index (static) and
   springs to the new one on appearing so it reads as one continuous
   control; header with brand, bell + badge, profile chip; phone layout
   via `OnIdiom` / width trigger (bottom floating capsule). Pushed pages
   keep the Shell back navigation, restyled.
7. **`ThemeReveal`** + theme toggle in rail/header; Settings appearance
   uses it.
8. **Pages, one per step**: Dashboard (Overview) → Live (Program) →
   Vehicles (Manage) → Alerts (Messages two-pane) → Reports → Plate
   history → Settings/Users/Gates/Data → Sign in. Each: new layout, Reveal
   orders, count-ups. Item templates bind page VMs via
   `{Binding Vm.X, Source={x:Reference Page}, x:DataType=p:XPage}` (no
   `RelativeSource AncestorType` — SourceGen inflator NREs).
9. **`HoverLift`** on cards (desktop) and Lucide SVG icons replace the
   tab icons.
10. **Verification**: `dotnet test` (Core), Windows build, run against
    `scripts/demo-stack.sh --hardened` (demo-admin / demo-open-sesame,
    server `127.0.0.1:8080`), screenshot every page in light and dark with
    `winscripts/shot.ps1`, capture short frame bursts of A3/A5/A7/A8/A11
    to compare timings against this catalogue; Reduce Motion pass; Android
    emulator build smoke test.

Environment for Phase 2:

* Windows dotnet: `"/mnt/c/Program Files/dotnet/dotnet.exe"`; build with
  Windows paths, e.g. `"/mnt/c/Program Files/dotnet/dotnet.exe" build
  'W:\home\deby\projects\gate-automation\app\src\GateControl.App' -f
  net10.0-windows10.0.19041.0` (W: = `\\wsl.localhost\Debian`). Verified
  in Phase 1: builds clean (0 warnings, 0 errors, ~17 s incremental).
* bin/obj → `%LOCALAPPDATA%\GateControl\build`; exe
  `C:\Users\User\AppData\Local\GateControl\build\bin\GateControl.App\Debug\net10.0-windows10.0.19041.0\win-x64\GateControl.exe`.
* UI automation: `ui.ps1` (UIA dump/click), `typepw.ps1` (keys),
  `shot.ps1` (PrintWindow → `%TEMP%\gc-shot.png`) in the session
  scratchpad `winscripts/`, run with `powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File "W:\tmp\...\winscripts\shot.ps1"`.
* Demo backend: `scripts/demo-stack.sh --hardened`; use `127.0.0.1`, not
  `localhost`, from Windows.

## 13. Reviewer decisions (Phase 1 review)

1. **Font:** bundle Inter (OFL) with tabular figures frozen in ("Inter
   GC"); system font as fallback only. Done (§4).
2. **Theme reveal:** snapshot + clip, 0.56 s ease-in-quad, wavy edge;
   overlay-scale only as a logged fallback; Reduce Motion 0.2 s
   cross-fade. Done (§11.7).
3. **Accent:** follow the video — orange only for brand, accent tile,
   hero, toggles, send button, unread/notification dot, selected-offer
   tint; ink for selection, nav pill, rail selection, primary buttons;
   no blue anywhere; caution `#F5A524`. Done (§5).
4. **Nav items:** content-width, three-part pill (two end caps on
   TranslationX + a middle bar on ScaleX, one pair of springs). Done.

## 14. Phase 2a — what was built and measured

**Shell.** One persistent `RootPage` for the signed-in session, inside a
`NavigationPage` (pushed pages — plate history, account, Users / Gates /
Data — keep the platform back stack). It builds header, rail and nav
once and hosts the five tab pages' content (each page is still created
by DI with its XAML and view model; its content is moved into the shell
with its view model pinned first, and its appearing/disappearing hooks
are forwarded, so loading, refresh timers and the live feed behave as
before). Wide (≥ 900 dip): brand capsule · centred NavCapsule · search,
bell (orange count badge = open alerts) · profile chip (→ Account); rail
with theme (sun/moon, ink selection circle), admin (Users, Gates, Data;
admins only) and account / sign-out capsules. Phone: compact header
(mark, theme toggle, search, bell, avatar) and the same NavCapsule
floating at the bottom. The old Shell (`AppShell.cs`) is removed. Pages
themselves are not restyled yet (Phase 2b), except where the new tokens
required it (avatar initials on ink in Settings; count-up and hover lift
on the Dashboard metric tiles to exercise those primitives).

**Verification harness.** DEBUG-only `Platform/Automation.cs` drives the
app from a command file (`tab n`, `theme dark|light`, `reduce on|off|os`,
`push settings`, `pop`, `plate X`, `size w h`, `info`) so motion is filmed
without touching the mouse; frames are grabbed with `PrintWindow`
(`tools/motion-capture/burst.ps1`, ≈ 30 fps at 1380×860 px) and lined up
with the app's own command log. Analysis scripts:
`tools/motion-capture/{pill,sheet,analyse}.py`. Evidence:
[`phase2a-evidence/`](phase2a-evidence/).

**Measured vs reference** (Windows 11, 1.5× scale; tolerance ±2 frames
at 30 fps):

| Animation | Reference (video) | Gate Control, measured | Result |
|---|---|---|---|
| A8 nav pill slide (Live → Dashboard, warm) | ζ 1.0, response 0.36 s; 90 % ≈ 0.20 s; settled ≈ 0.37 s | ζ 1.0, response **0.38 s** (fit err 0.005); within 1 px at 338 ms | pass |
| A8 interrupted (Alerts, then Dashboard 160 ms later) | — | pill turns around from where it is (x = 346 px, still moving right) and settles on Dashboard; no restart, no jump (`strip-pill-interrupted.jpg`) | pass |
| Shell flicker during tab switches | — | brand, actions and rail: **0** pixel change across 102 frames of three bursts | pass |
| A3 card reveal (segmented / metric tiles / charts) | (1.0, 0.40) → 90 % ≈ 0.27 s after a card starts; 0.12 s apart | 90 % **267 / 266 / 256 ms** after each starts; starts at 139 / 206 / 449 ms | pass (timing); stagger: the charts start late because they wait for data |
| A11 theme reveal, dark → light | area 1 % → 98.5 % in 0.50 s (15 frames) | **466 ms** | pass |
| A11 theme reveal, light → dark | same | **434 ms** (13 frames; 2 frames fast) | pass (at the edge) |
| A5 count-up, Dashboard first load | OutCubic 1.45 s | digits change from 360 ms to 1395 ms after the tab switch (data arrives first); consistent with a 1.45 s OutCubic whose last integer step comes at u ≈ 0.8 | consistent (no per-frame value read-out on Windows) |
| A5 horizontal jitter | — | number ink edges stay within ±1 px for two-digit values over 60 frames (glyph ink, not advance; advances are identical) | pass |
| A7 bar growth | per-bar 0.065 s stagger | not built in 2a (ChartView is step 5 of the plan, Phase 2b) | — |
| Reduce Motion (DEBUG override) | — | tab switch: content appears in one quick fade, pill jumps; theme: 0.2 s cross-fade, no wipe (`bursts/ev-rm-*`) | pass |

Not verified in 2a: Android runtime (blur cost, RenderEffect frame
times) — the API 34 emulator never came online (adb "offline" after
20 min of cold boot under WHPX), so the Android build compiles but was not
run; Windows' own "Animation effects" switch was not toggled (it is a
user setting on a shared machine) — the same code path was driven
through `MotionSettings.Override`, and detection itself is unchanged
(`UISettings.AnimationsEnabled`).

## 15. Phase 2b — pages, review fixes, smoothness

**User requests.** (1) The light/dark toggle sits at the **top-right end** of
the header on every layout (its own capsule after the profile chip; phones
the same), and the wipe starts there. The rail keeps the admin and account
capsules. (2) Page-load animation smoothness is measured frame by frame (below).

**Review changes 1–12.**
1. Font: `freeze_tnum.py` now freezes `tnum` for the digits U+0030–0039 only;
   hyphen, colon, comma and period keep their proportional glyphs ("gate-demo-01").
2. Theme reveal: snapshot on pointer-down, native (RenderTargetBitmap shown
   as the native Image source; Android View.Draw → Bitmap), no PNG round-trip,
   theme switch after the snapshot's first rendered frame
   (CompositionTarget.Rendering / Choreographer), no fixed delays; a
   pointer-down snapshot older than 1 s is discarded. First wipe frame 21–24
   ms after the tap; area 1 % → 98.5 % in 484 / 499 / 527 ms (target 500 ± 33).
   The radius curve's duration is 0.62 s, calibrated on that area metric (the
   0.56 s radius curve of the video covers our wider window faster).
3. Fallbacks and failures log through `AppLog` (ILogger + a file sink at
   `<app data>/gatecontrol.log`, Release included); WinUI unhandled
   exceptions are logged with their stack.
4. `MotionSettings.Override` and the whole harness exist only in Debug and in
   the `-p:GcHarness=true` measurement build (verified absent from Release).
5. Tabs are parked: until its first visit a tab is laid out, its data loaded
   and kept drawn off-window at 1/255 opacity (idle priority, one tab per
   slot, 1.2 s after the first tab); once visited it is parked collapsed.
   A preloaded tab's data refresh runs 1.2 s after the entrance, not during it.
6. Stagger: `Reveal.Order` counts per kind (cards 120 ms, rows 65 ms, words
   90 ms); every item's start is traced (table in `phase2b-evidence/timing-table.txt`).
7. The window caption uses a MAUI `TitleBar` coloured from the tokens per theme.
8. Focus: initial focus is parked programmatically, so no ring at rest;
   keyboard focus still shows WinUI's (ink) focus visual.
9. Gate cards (and all KPI tiles) use `TileGrid`: equal-width columns that
   fill the row; on phones the gate cards span the full column.
10–11. Android: the API 34 and a new API 36.1 AVD were cold-booted
   (`-no-snapshot-load`, `-gpu host` and `swiftshader_indirect`, adb
   kill/start-server). The guest never came up (adb "offline"; the emulator
   log reports missing CPUID features under WHPX — this Windows host also
   runs VirtualBox). No physical device was attached. So `-t:Run`, the
   gfxinfo blur on/off comparison and the "Remove animations" check on
   Android were **not done**. The Android build compiles 0/0.
12. `PlatformBlur.Enabled` defaults to false.

**Pages (§10).** Dashboard (greeting from the user's name and local time,
lead KPI as the accent tile with the A6 blob reveal, KPI tiles with
count-ups, stacked hourly bars A7, "Most frequent" table); Live (hero tally
with count-up and allowed-share bar, gate control cards, feed as a history
list with drop-in kept); Vehicles (Manage pattern, sheet and swipe-to-remove
kept); Alerts (two-pane; clear one / clear all kept); Reports (header kind
picker, tables, two-pane plate history ≥ 820 dip); plate history
(`Views/PlateHistoryView`, shared by the pushed page and both panes);
Settings, Users, Gates, Data and Sign-in restyled. Side-by-sides against the
reference frames, light and dark: `phase2b-evidence/compare/`.

**Bugs found and fixed on the way.** Win2D-drawn shapes crashed the app
("Element not found" while clipping by some glyphs): icons are now native
Border strokes and the round icon backgrounds are Borders, not Ellipses; two
glyphs the native renderers dropped are redrawn (`IconsExtra`). WinUI kept
the hosted tabs at a stale width (content past the right edge): hosted tabs
get the host's exact size, ScrollView / RefreshView content is pinned to
their width, nested wrapping FlexLayouts replaced by `TileGrid`. Screen-reader
announce on the initial tab threw during window realisation. Parked tabs
showed through faintly (layers that ignore parent opacity): parked off-window.

### Page-load smoothness

Every tab entrance, first and later visit, traced in-app on every rendered
frame (240 Hz display): frame times, and each animated item's opacity, rise
and scale. The motion ticker now runs once per displayed frame
(CompositionTarget.Rendering / Choreographer) instead of a ~64 Hz timer, so
items move every frame. Results (dropped frames against a 60 Hz budget):
5 of 9 entrance types meet "0, at most 1" (Dashboard later 0, Reports first
0, Vehicles later 0, Alerts later 0, Live later 1); Reports later 2, Vehicles
first 2, Alerts first 2, Dashboard first 4 — single 40–54 ms frames, mostly
the switch frame itself. Card steps 117–137 ms (one 167 ms step behind a
stall on Dashboard's first visit). Every item's opacity and rise are
monotonic; no item is visible before its reveal; late-arriving rows join the
stagger (first 12 only). Reduce Motion: one fade, 0 dropped frames.
Fixes that got here: no scale on tall or chart cards (re-rendering text /
canvases every frame), content kept rasterised while hidden (opacity 1/255,
not 0), count-up labels pinned to their final size while counting (no
page-wide layout per frame), unchanged data not rebuilt (`ReplaceWith`
compares), refresh deferred until after the entrance, charts on the frame
ticker. Full table: `phase2b-evidence/timing-table.txt`; traces, analyses and
burst contact sheets: `phase2b-evidence/smoothness/`.

**Regression pass (Windows, demo stack).** Gate commands Open / Close
change the simulated gate (verified via `/api/status`); Hold open, Lock down
(with their confirmations) and Release are sent and acknowledged ("Sent")
— the simulator keeps reporting CLOSED for latches. Feed rows arrive and
drop in. Search (plate filter: 50 rows), paging (Show more: 100 rows), CSV
export (51-line file written, share sheet opened). Plate history pushed and
in both panes. Roles: viewer (no gate buttons, no admin rail, Vehicles gated),
operator (gate buttons, no admin rail), admin (all) — users created through
the Users sheet (operator via the role picker; the viewer's role set through
the API because the picker defaults to Operator). Alerts badge (11) and clear
all (11 → 0). Sign-out / sign-in for each role. No crash on any tab with
two-way pickers. Haptics: no engine on Windows (unchanged no-op).

## 16. Phase 2b round 2 — review items

**Platforms.** Windows: built Debug and Release 0 warnings / 0 errors, and
run. **Android: built, not run-tested; needs a device.** Debug and Release
build 0/0. The emulator does not boot on this host (WHPX CPUID), and no device
was attached. Every `#if ANDROID` branch was read: Choreographer ticker (one
callback chain, `_posted` guard), Reduce Motion from `AnimatorDurationScale`
== 0, haptics (`Confirm` / `KeyboardTap` / `Reject` on API 30+, MAUI
`HapticFeedback` below, VIBRATE declared), theme snapshot by PixelCopy with a
View.Draw fallback that recycles on failure, borderless entry/picker
backgrounds, and blur (`PlatformBlur.Enabled` false by default). The release
DLL has no harness: the `GC_AUTOMATION_FILE` string, `set_Override`,
`StartTrace` and `TickTrace` are absent (`phase2b-evidence/round2/builds/`).

**Smoothness changes this round.** Every long frame was attributed (layout,
GC, data, compositor) with in-app spans (`Motion/FrameTrace.cs`, harness
builds only). Kept: rows of long result lists parked during their card's fade
(Reports later 4 → 0 dropped); each parked tab rendered once off screen after
it preloads (Windows); the preload wait capped at 2 s; the harness's trace
buffers allocated once. Superseded by §17: the forced gen-2 in the swap frame
and `HighMemoryPercent=97` (both removed; the collections were induced, and
are fixed at their sources). Final results: §17.

**Functional.** After Hold / Lockdown / Release, the gate tile shows the
simulator's "unsupported on the simulated residential profile" failure in red,
and the Error haptic is logged. New users default to Viewer (Core test
`NewUsersDefaultToViewer`, UI screenshot). Alerts clear-one: 3 → 2. Settled
screenshots of every tab in both themes show no ghosting, the hour labels on
Vehicles-by-hour, and a filled last tile row on the Dashboard
(`phase2b-evidence/round2/`).

## 17. Phase 2b round 2 — reviewer required fixes

Status: **every acceptance row passes** in the final run (run D, 2026-10-29, mains power,
5 launches): median 0 / worst 0 dropped frames on all 9 entrances and both Reduce Motion
entrances, longest frame in any reveal window 24.8 ms, tap → first pill-motion frame median
4.3 ms, worst 10.9 ms (≤ 17 ms). The run immediately before it (D0, same app code) had one
failing row (Alerts later, one background gen-2 at 1263 ms, worst 2 dropped); its cause and what
remains of it are below. Run C (2026-10-24, another agent) failed five rows and is superseded.
Tables: `phase2b-evidence/round2/acceptance-table.md`; GC traces:
`phase2b-evidence/round2/gc-traces/`.

1. **No forced collections.** The forced gen-2 on tab switch and after prewarm are gone
   (`RootPage.cs`); so are `GCLatencyMode.SustainedLowLatency` (MauiProgram) and
   `System.GC.HighMemoryPercent` (csproj). None of the collections that cost frames was
   memory-load or allocation driven; all were *induced*, which neither setting prevents.
2. **The first-entrance gen-2, by cause** (EventPipe, `dotnet-trace
   Microsoft-Windows-DotNETRuntime:0x1:5`, parsed with `tools/motion-capture/trace-parser`,
   GCTriggered stacks):
   - InducedNotForced from MAUI's `UpdateTransformation`: a new WinUI `CompositeTransform` (and
     `ScaleTransform`) per TranslationX/Y / Scale write, per element per frame. Fix:
     `Platform/WinTransform.cs` updates one transform per element in place; `Reveal.Warm`
     creates them while the tab is parked.
   - Per-frame WinUI objects from the MAUI blob clip (WrapperView rebuilt its geometry each
     frame). Fix: on Windows the A6 blob is one composition path built once (Win2D) and the
     clip's scale and rotation move (`Motion/BlobClip.cs`, NativeClip); a wave phase φ is the same
     outline turned by −φ/5, so the shape is unchanged.
   - InducedNotForced from `W2DCanvas.DrawString`, which creates a WinUI `FontFamily` per call
     (chart axis labels, per frame while the bars grow). Fix: `ChartView.Text` draws on the Win2D
     session with a `CanvasTextFormat` made once per size.
   - Induced (blocking, 45–80 ms) from WinUI itself, `IReferenceTrackerHost.
     DisconnectUnusedReferenceSources`. Source (microsoft-ui-xaml, release 1.8.8,
     `CCoreServices::TrackMemory`): every 50 frames that need a render walk, XAML calls a blocking
     GC if its native heap grew > 50 MB since the last low point. An idle app draws almost no such
     frames, so after launch and the tab warm-up the check fell due 70–250 ms into the first
     entrance. Fix: `RootPage.SettleXamlCleanup` — once the tabs are warm, the parked tabs (outside
     the window) are nudged by half a dip for 120 frames, so the check, and its collection if due,
     happens at idle. Parked tabs now keep opacity 1 off-window (they were at 0, which made WinUI
     drop and rebuild their render data: a 25–29 ms first frame on every entrance).
   - Not the cause (tested): the accessibility re-include on unpark, the glow image, Reduce
     Motion, the harness (the GC also occurred with no frame trace attached).
3. **Remaining, documented:** the CompositionTarget.Rendering event creates one WinRT
   event-args wrapper per frame; their memory pressure can induce a *background* gen-2 (FR3
   trace: InducedNotForced, one 39 ms frame in a 40 s run). The harness's second Rendering
   handler is gone (it samples on the app ticker, as production has one handler); the production
   ticker still needs its one handler, so this can still happen occasionally while things animate.
   It did not occur in run D's 45 entrances.
4. **Tap → first pill-motion frame** is measured from the `tab.select` mark to the first frame
   tick after the pill's first `nav.apply`, for every entrance.
5. **Acceptance hygiene:** each run signs out and back in, and a run whose traces are not real
   entrances is repeated (`acceptance.py --check`).
6. **Builds and tests (2026-10-29):** Core tests 105/105; Windows Debug + Release and Android
   Debug + Release 0 warnings / 0 errors (`phase2b-evidence/round2/builds/`); the Release DLL
   contains no harness (`release-harness-check.txt`). Android: built, not run-tested; needs a
   device.

### Round 3 (interim review) and the theme reveal re-check — 2026-10-30

- **Entrances (run E, mains, 5 launches, Release + harness):** every row passes. That is
  median 0 / worst 0 dropped on all 9 entrances and both Reduce Motion entrances, no frame over
  25 ms, and tap → pill motion at a median of 4.0 ms (worst 8.7 ms). Every acceptance run used
  the Release build *with the harness compiled in*; the shipped binary does less per frame.
- **Theme reveal** (`phase2b-evidence/round2/theme-reveal.md`, 5 runs per direction, wide and
  phone): all rows pass.
  - The disc starts at the top-right toggle on both layouts.
  - Tap → first wipe frame: median 24–33 ms, worst 91 ms.
  - Coverage (area 1 % → 98.5 %): 487–492 ms wide and 521 ms on the phone layout, inside
    500 ± 33 ms.
  - The wavy edge's phase runs 0 → 2.51 rad over 145–150 wipe frames.
  - 0 dropped frames during every wipe; longest wipe frame 7.9 ms (was 25–132 ms).
- **Theme reveal changes:**
  1. The snapshot was wrong: a render of a window whose parked tabs stretched its bounds, so
     the image came out ~21000 dip wide. The host is now clipped while a reveal runs.
  2. The theme is applied under the snapshot during the press. The 40–50 ms switch plus the
     60–130 ms restyle frame used to delay the wipe to 100–144 ms after the tap.
  3. The parked tabs are dropped from rendering during the reveal and brought back at idle.
  4. On Windows the hole is one composition path whose clip scales and turns. The MAUI clip,
     rebuilt every frame, caused 25–40 ms gen-0/1 collections mid-wipe.
  5. The caption switches on the first wipe frame.
  6. A resize ends a running wipe.
  7. A press with no tap within 1 s is undone.
- **Theme edge cases:** tap again mid-wipe, tab switch mid-wipe, resize mid-wipe, press held
  over 1 s, press without a tap, and Reduce Motion (200 ms cross-fade) were all tested. Every
  case ended in the right theme and caption, with no overlay left and nothing pending.
- **Review items 0–5:** `phase2b-evidence/round2/round3-review-items.md`.
  - Keyboard focus and UI Automation no longer reach parked tabs.
  - WinTransform removes its transform from an inner view once a container appears.
  - Anchor / RTL / Reduce Motion parity with stock MAUI holds.
  - SettleXamlCleanup re-arms on resize and theme changes.

