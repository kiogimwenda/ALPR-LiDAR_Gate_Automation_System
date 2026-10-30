# trace_analyse.py <frames.csv> <items.csv> [label] — page-entrance smoothness from the app's own
# per-frame trace (Automation "trace"): frame pacing / dropped frames, per-item start times and
# stagger, and the shape of every item's opacity / rise curve against the spring it should follow.
import csv, math, sys, json

def spring(t, z=1.0, r=0.4):
    if t <= 0: return 0.0
    w = 2 * math.pi / r
    return 1 - math.exp(-w * t) * (1 + w * t)

frames_csv, items_csv = sys.argv[1], sys.argv[2]
label = sys.argv[3] if len(sys.argv) > 3 else ''
fr = [float(r['ms']) for r in csv.DictReader(open(frames_csv))]
iv = [b - a for a, b in zip(fr, fr[1:])]
iv_sorted = sorted(iv)
nominal = iv_sorted[len(iv_sorted) // 2] if iv else 16.7
nominal = min(nominal, 17.0) if nominal < 20 else nominal   # 60 Hz display
# The display runs at `nominal` (240 Hz here); the budget that matters is a
# 60 Hz frame (16.7 ms): a frame that took longer dropped round(d/16.7)-1
# 60 Hz frames (d ≥ 25 ms drops at least one).
BUDGET = 1000 / 60
dropped = sum(max(0, round(d / BUDGET) - 1) for d in iv)
long_frames = [round(d, 1) for d in iv if d > BUDGET]
p90 = iv_sorted[int(0.9 * (len(iv_sorted) - 1))] if iv else 0

items = {}
for r in csv.DictReader(open(items_csv)):
    it = items.setdefault(int(r['item']), {'kind': r['kind'], 'delay': float(r['delay_ms']), 's': []})
    it['s'].append((float(r['frame_ms']), float(r['opacity']), float(r['translation_y']), float(r['scale']), int(r['clip'])))

# the entrance clock: the first frame of the trace that already shows the items hidden
t0 = fr[0] if fr else 0
rows = []
issues = []
for k in sorted(items):
    it = items[k]; s = it['s']
    if not s: continue
    kind = it['kind']
    first_op = s[0][1]
    if kind == 'accent':
        start = next((t for t, o, y, sc, c in s if c == 1), None)
        end = next((t for t, o, y, sc, c in s if c == 0 and t > (start or 0)), None)
        rows.append({'item': k, 'kind': kind, 'delay_ms': it['delay'], 'start_ms': round((start or 0) - t0, 1),
                     'note': f'blob clip until {round((end or 0) - t0)} ms'})
        continue
    # visible start = first frame with opacity > 1 %
    start = next((t for t, o, y, sc, c in s if o > 0.01), None)
    mono = all(b[1] >= a[1] - 1e-3 for a, b in zip(s, s[1:]))
    rise = all(abs(b[2]) <= abs(a[2]) + 1e-2 for a, b in zip(s, s[1:]))
    # largest per-frame opacity step vs the steepest step the spring can
    # make over that frame time (critically damped, response 0.40 s: peak
    # slope ω/e per second; a sample can span two ticks, so >2x means a jump the spring can't explain.
    w = 2 * math.pi / 0.4
    peak = w / math.e
    worst = 0.0
    for a, b in zip(s, s[1:]):
        dt = (b[0] - a[0]) / 1000
        worst = max(worst, (b[1] - a[1]) / (peak * dt + 0.004))
    flash = first_op > 0.05 and it['delay'] > 0
    if not mono: issues.append(f'item {k}: opacity not monotonic')
    if not rise: issues.append(f'item {k}: rise not monotonic')
    if flash: issues.append(f'item {k}: visible before its reveal (opacity {first_op:.2f} at first frame)')
    if worst > 2.0: issues.append(f'item {k}: a frame step {worst:.1f}x the steepest spring step')
    t90 = next((t for t, o, y, sc, c in s if o >= 0.9), None)
    rows.append({'item': k, 'kind': kind, 'delay_ms': it['delay'],
                 'start_ms': round(start - t0, 1) if start else None,
                 't90_after_start_ms': round(t90 - start, 1) if (t90 and start) else None,
                 'monotonic': mono and rise, 'max_step_vs_spring': round(worst, 2)})

cards = sorted([r for r in rows if r['kind'] in ('card', 'accent') and r['start_ms'] is not None], key=lambda r: r['delay_ms'])
steps = [round(b['start_ms'] - a['start_ms'], 1) for a, b in zip(cards, cards[1:]) if b['delay_ms'] != a['delay_ms']]
out = {'label': label, 'frames': len(fr), 'duration_ms': round(fr[-1] - fr[0], 1) if fr else 0,
       'nominal_frame_ms': round(nominal, 2), 'p90_frame_ms': round(p90, 2), 'max_frame_ms': round(max(iv), 1) if iv else 0,
       'dropped_frames_60hz': dropped, 'frames_over_16_7ms': long_frames, 'card_steps_ms': steps,
       'items': rows, 'issues': issues}
print(json.dumps(out, indent=1))
