# theme_analyse.py <prefix> <runs> — the theme wipe from in-app traces (theme_runs.sh):
# tap → first wipe frame, coverage time (first wipe frame → the first frame whose wavy disc
# contains the whole window), wipe duration, dropped frames (60 Hz budget) and the longest frame
# from the tap to the overlay's removal, wave phase advancing, origin vs the toggle.
import csv, math, os, statistics, sys
D = '/mnt/c/Users/User/AppData/Local/Temp'
PRE = sys.argv[1] if len(sys.argv) > 1 else 'TH'
RUNS = int(sys.argv[2]) if len(sys.argv) > 2 else 5
BUDGET = 1000 / 60
AMP, LOBES = 0.06, 5

def covered(r, phi, w, h, ox, oy):
    # every window-edge point (every 4 dip) inside r·(1 + 0.06·sin(5θ + φ))
    pts = [(x, 0) for x in range(0, int(w) + 1, 4)] + [(x, h) for x in range(0, int(w) + 1, 4)] + \
          [(0, y) for y in range(0, int(h) + 1, 4)] + [(w, y) for y in range(0, int(h) + 1, 4)]
    for x, y in pts:
        dx, dy = x - ox, y - oy
        th = math.atan2(dy, dx)
        if math.hypot(dx, dy) > r * (1 + AMP * math.sin(LOBES * th + phi)):
            return False
    return True

def area(r, phi, w, h, ox, oy, n=120):
    # covered fraction of the window, sampled on an n × (n·h/w) grid
    m = max(1, int(n * h / w)); inside = 0
    for i in range(n):
        for j in range(m):
            dx, dy = (i + 0.5) * w / n - ox, (j + 0.5) * h / m - oy
            if math.hypot(dx, dy) <= r * (1 + AMP * math.sin(LOBES * math.atan2(dy, dx) + phi)):
                inside += 1
    return inside / (n * m)

def one(p):
    fr = [float(x['ms']) for x in csv.DictReader(open(os.path.join(D, f'gc-{p}-frames.csv')))]
    ev = list(csv.DictReader(open(os.path.join(D, f'gc-{p}-events.csv'))))
    t = lambda name: next((float(e['ms']) for e in ev if e['event'] == name), None)
    tap, end, press = t('theme.tap'), t('theme.end'), t('theme.prepare')
    # The tap command is written 90 ms after the press; if the UI thread is busy
    # (the restyle) it is handled late, so measure from press + 90 ms when that is earlier.
    if press is not None and tap is not None:
        tap = min(tap, press + 90)
    wipes = [(float(e['ms']), *map(float, e['event'].split()[1:])) for e in ev if e['event'].startswith('theme.wipe ')]
    snap = next((e['event'] for e in ev if e['event'].startswith('theme.snapshot')), '?')
    if tap is None or not wipes or end is None:
        return None
    first = wipes[0][0]
    cov = next((tm for tm, r, phi, w, h, ox, oy in wipes if covered(r, phi, w, h, ox, oy)), None)
    fr_area = [(tm, area(r, phi, w, h, ox, oy)) for tm, r, phi, w, h, ox, oy in wipes]
    a1 = next((tm for tm, a in fr_area if a >= 0.01), None); a98 = next((tm for tm, a in fr_area if a >= 0.985), None)
    # the wipe itself: from its first frame until the overlay is removed
    iv = [(b, b - a) for a, b in zip(fr, fr[1:]) if first + 1 < b <= end + 1]
    # before it: the press-time restyle, drawn under the static snapshot
    pre = max((b - a for a, b in zip(fr, fr[1:]) if (press or tap) < b <= first + 1), default=0)
    dropped = sum(max(0, round(d / BUDGET) - 1) for _, d in iv)
    phis = [w[2] for w in wipes]
    return dict(latency=first - tap, cover=(cov - first) if cov else None, area=(a98 - a1) if a1 and a98 else None, dur=end - first, dropped=dropped,
                longest=max(d for _, d in iv), pre=pre, frames=len(wipes), phase=(phis[0], phis[-1]),
                origin=(wipes[0][5], wipes[0][6]), size=(wipes[0][3], wipes[0][4]), snap=snap)

rows = []
for key, label in (('l', 'dark → light'), ('d', 'light → dark')):
    res = [x for x in (one(f'{PRE}{r}-{key}') for r in range(1, RUNS + 1) if os.path.exists(os.path.join(D, f'gc-{PRE}{r}-{key}-frames.csv'))) if x]
    if not res:
        continue
    lat = [x['latency'] for x in res]; cov = [x['cover'] for x in res if x['cover'] is not None]
    drp = [x['dropped'] for x in res]; lng = [x['longest'] for x in res]
    ar = [x['area'] for x in res if x['area'] is not None]
    ok = max(lat) <= 100 and all(abs(c - 500) <= 33 for c in ar) and len(ar) == len(res) and statistics.median(drp) == 0 and max(drp) <= 1 and max(lng) <= 33.4
    rows.append(f"| {label} | {len(res)} | {statistics.median(lat):.1f} / {max(lat):.1f} | "
                f"{statistics.median(ar):.0f} ({min(ar):.0f}–{max(ar):.0f}) | {statistics.median(cov):.0f} | {statistics.median([x['dur'] for x in res]):.0f} | "
                f"{statistics.median(drp):g} / {max(drp)} | {max(lng):.1f} | {max(x['pre'] for x in res):.0f} | {res[0]['frames']} | "
                f"{res[0]['phase'][0]:.2f}→{res[0]['phase'][1]:.2f} | {res[0]['origin'][0]:.0f},{res[0]['origin'][1]:.0f} in {res[0]['size'][0]:.0f}×{res[0]['size'][1]:.0f} | "
                f"{', '.join(sorted(set(x['snap'].replace('theme.snapshot.', '') for x in res)))} | {'pass' if ok else 'FAIL'} |")
print('| Direction | runs | tap → first wipe frame, median / worst (ms) | coverage: area 1 % → 98.5 % (ms, median, range) | whole window inside the disc (ms after first wipe frame) | wipe to overlay removed (ms) | dropped during the wipe, median / worst | longest wipe frame (ms) | longest frame under the static snapshot before the wipe (ms) | wipe frames | wave phase φ | origin (dip) | snapshot | result |')
print('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
print('\n'.join(rows))
