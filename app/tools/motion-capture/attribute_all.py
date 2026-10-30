# attribute_all.py [runs] — classify every frame > 25 ms of the acceptance traces by cause:
# gc (a collection completed in that frame), ui (traced UI-thread work ≥ 5 ms: motion tick,
# property writes, nav), data (item creation / data refresh), compositor (< 2 ms of traced
# UI-thread work — the time went to rendering), other.
import csv, os, sys, collections
D = '/mnt/c/Users/User/AppData/Local/Temp'
RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 5
keys = ['F-1', 'F-2', 'F-3', 'F-4', 'L-0', 'L-1', 'L-2', 'L-3', 'L-4']
total = collections.Counter(); per = collections.defaultdict(collections.Counter); details = collections.defaultdict(list)
for k in keys:
    for r in range(1, RUNS + 1):
        p = f'A{r}-{k}'
        f = os.path.join(D, f'gc-{p}-frames.csv')
        if not os.path.exists(f): continue
        fr = list(csv.DictReader(open(f)))
        ev = [(float(e['ms']), float(e['dur_ms']), e['event']) for e in csv.DictReader(open(os.path.join(D, f'gc-{p}-events.csv')))]
        for a, b in zip(fr, fr[1:]):
            t0, t1 = float(a['ms']), float(b['ms'])
            if t1 - t0 <= 25: continue
            inside = [e for e in ev if t0 < e[0] <= t1]
            ui = sum(d for _, d, n in inside if n in ('motion.tick',))
            gc = int(b['gc0']) != int(a['gc0'])
            data = any(n in ('element.added', 'data.load.end', 'tab.refresh', 'item.join') for _, _, n in inside)
            cause = 'gc' if gc else 'ui' if ui >= 5 else 'data' if data else 'compositor' if ui < 2 else 'other'
            total[cause] += 1; per[k][cause] += 1
            details[cause].append((p, round(t1 - float(fr[0]['ms'])), round(t1 - t0)))
print('| Entrance | frames > 25 ms | compositor | gc | ui | data | other |')
print('|---|---|---|---|---|---|---|')
for k in keys:
    c = per[k]; print(f"| {k} | {sum(c.values())} | {c['compositor']} | {c['gc']} | {c['ui']} | {c['data']} | {c['other']} |")
c = total; print(f"| all | {sum(c.values())} | {c['compositor']} | {c['gc']} | {c['ui']} | {c['data']} | {c['other']} |")
for cause in ('gc', 'ui', 'data', 'other'):
    if details[cause]: print(f'\n{cause}: ' + ', '.join(f'{p}@{t}ms({d})' for p, t, d in details[cause][:20]))
