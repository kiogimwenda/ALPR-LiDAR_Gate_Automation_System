# acceptance.py [runs] — median / worst dropped frames (60 Hz budget) per entrance over the runs,
# and the longest frame inside the reveal window (from the swap until every traced item has
# settled: opacity ≥ 0.999 and rise ≤ 0.05 dip).
import csv, os, statistics, sys
D = '/mnt/c/Users/User/AppData/Local/Temp'
def valid(prefix):
    # A real entrance: the tab was selected, and (with motion) items were traced.
    ev = os.path.join(D, f'gc-{prefix}-events.csv'); it = os.path.join(D, f'gc-{prefix}-items.csv')
    if not (os.path.exists(ev) and os.path.exists(it)):
        return False
    if not any(e['event'] == 'tab.select' for e in csv.DictReader(open(ev))):
        return False
    return '-RM-' in prefix or len({r['item'] for r in csv.DictReader(open(it))}) > 1
if len(sys.argv) > 2 and sys.argv[1] == '--check':
    r = sys.argv[2]
    keys = ['F-1', 'F-2', 'F-3', 'F-4', 'L-0', 'L-1', 'L-2', 'L-3', 'L-4', 'RM-1', 'RM-3']
    bad = [k for k in keys if not valid(f'A{r}-{k}')]
    if bad:
        print('invalid traces:', ', '.join(bad))
    sys.exit(1 if bad else 0)
RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 5
BUDGET = 1000 / 60
names = [('F-1', 'Dashboard, first visit'), ('F-2', 'Reports, first visit'), ('F-3', 'Vehicles, first visit'),
         ('F-4', 'Alerts, first visit'), ('L-0', 'Live, later visit'), ('L-1', 'Dashboard, later visit'),
         ('L-2', 'Reports, later visit'), ('L-3', 'Vehicles, later visit'), ('L-4', 'Alerts, later visit'),
         ('RM-1', 'Dashboard, Reduce Motion'), ('RM-3', 'Vehicles, Reduce Motion')]
def one(prefix):
    fr = [float(r['ms']) for r in csv.DictReader(open(os.path.join(D, f'gc-{prefix}-frames.csv')))]
    iv = [b - a for a, b in zip(fr, fr[1:])]
    dropped = sum(max(0, round(d / BUDGET) - 1) for d in iv)
    settle = fr[0]
    items = {}
    for r in csv.DictReader(open(os.path.join(D, f'gc-{prefix}-items.csv'))):
        items.setdefault(r['item'], []).append((float(r['frame_ms']), float(r['opacity']), float(r['translation_y']), int(r['clip'])))
    for s in items.values():
        last = next((t for t, o, y, c in reversed(s) if not (o >= 0.999 and abs(y) <= 0.05 and c == 0)), fr[0])
        settle = max(settle, last)
    win = [(b - a) for a, b in zip(fr, fr[1:]) if a <= settle]
    return dropped, max(win) if win else 0, settle - fr[0]
rows = []
for key, label in names:
    res = []
    for r in range(1, RUNS + 1):
        p = f'A{r}-{key}'
        if os.path.exists(os.path.join(D, f'gc-{p}-frames.csv')):
            res.append(one(p))
    if not res:
        continue
    drops = [x[0] for x in res]; wmax = [x[1] for x in res]
    med = statistics.median(drops); worst = max(drops)
    ok = med == 0 and worst <= 1 and max(wmax) <= 33.4
    rows.append(f"| {label} | {len(res)} | {med:g} | {worst} | {', '.join(str(d) for d in drops)} | {max(wmax):.1f} | {statistics.median([x[2] for x in res]):.0f} | {'pass' if ok else 'FAIL'} |")

# Tap → first pill-motion frame (target ≤ 1 frame, ≤ 17 ms): from the tab
# selection (mark "tab.select", where a tap lands in RootPage.Select) to the
# first frame tick (CompositionTarget.Rendering) after the pill's first
# move (span "nav.apply"). Every traced non-Reduce-Motion entrance counts.
pill_latencies = []
for r in range(1, RUNS + 1):
    for key, _ in names:
        if key.startswith('RM'):
            continue
        p = f'A{r}-{key}'
        ff = os.path.join(D, f'gc-{p}-frames.csv')
        fe = os.path.join(D, f'gc-{p}-events.csv')
        if not (os.path.exists(ff) and os.path.exists(fe)):
            continue
        frames = [float(x['ms']) for x in csv.DictReader(open(ff))]
        ev = list(csv.DictReader(open(fe)))
        sel = next((float(e['ms']) for e in ev if e['event'] == 'tab.select'), None)
        if sel is None:
            continue
        moved = next((float(e['ms']) for e in ev if e['event'] == 'nav.apply' and float(e['ms']) >= sel), None)
        if moved is None:
            continue
        shown = next((t for t in frames if t >= moved), None)
        if shown is not None:
            pill_latencies.append(shown - sel)

if pill_latencies:
    med_pill = statistics.median(pill_latencies)
    worst_pill = max(pill_latencies)
    ok_pill = worst_pill <= 17.0
    rows.append(f"| Tap → first pill-motion frame (ms) | {len(pill_latencies)} | {med_pill:.1f} | {worst_pill:.1f} | (all {len(pill_latencies)} entrances) | — | — | {'pass' if ok_pill else 'FAIL'} |")

print('| Entrance | runs | median dropped | worst dropped | per run | longest frame in reveal window (ms) | reveal window (ms, median) | result |')
print('|---|---|---|---|---|---|---|---|')
print('\n'.join(rows))
