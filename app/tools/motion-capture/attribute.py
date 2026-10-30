# attribute.py <prefix> — for every long frame (> 25 ms) of a trace, what ran between the previous
# presented frame and it: events by kind with counts and timed durations (FrameTrace).
import csv, sys, os, collections
p = sys.argv[1]
d = sys.argv[2] if len(sys.argv) > 2 else '/mnt/c/Users/User/AppData/Local/Temp'
fr = [float(r['ms']) for r in csv.DictReader(open(os.path.join(d, f'gc-{p}-frames.csv')))]
ev = [(float(r['ms']), float(r['dur_ms']), r['event']) for r in csv.DictReader(open(os.path.join(d, f'gc-{p}-events.csv')))]
t0 = fr[0]
for i in range(1, len(fr)):
    gap = fr[i] - fr[i - 1]
    if gap <= 25: continue
    inside = [e for e in ev if fr[i - 1] < e[0] <= fr[i]]
    agg = collections.OrderedDict()
    for t, du, k in inside:
        c = agg.setdefault(k, [0, 0.0]); c[0] += 1; c[1] += du
    desc = ', '.join(f'{k}×{n}' + (f' {s:.1f}ms' if s > 0.05 else '') for k, (n, s) in agg.items())
    print(f'{p}: frame at +{fr[i]-t0:.0f} ms took {gap:.1f} ms: {desc or "(nothing traced)"}')
