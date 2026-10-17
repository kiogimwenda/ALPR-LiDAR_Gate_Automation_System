"""Route one remaining connection with a 2-layer grid A* (F.Cu/B.Cu), then
verify every segment exactly and refill zones.
python.exe maze.py in.kicad_pcb out.kicad_pcb NET REF_A PIN_A REF_B PIN_B x0 y0 x1 y1 [width]
(bbox in KiCad absolute mm)"""
import heapq
import math
import sys

import pcbnew

from geo import Copper, pad_xy

inp, outp, NET, ra, pa, rb, pb = sys.argv[1:8]
x0, y0, x1, y1 = map(float, sys.argv[8:12])
WID = float(sys.argv[12]) if len(sys.argv) > 12 else 0.2
G = 0.1                                   # grid pitch, mm
VIA_COST = 6.0
b = pcbnew.LoadBoard(inp)
A, Bp = pad_xy(b, ra, pa), pad_xy(b, rb, pb)
# put the (fine-pitch) start pad exactly on a grid node
x0 = A[0] - round((A[0] - x0) / G) * G
y0 = A[1] - round((A[1] - y0) / G) * G
cu = Copper(b, clr=0.205)
# local obstacle set only
cu.objs = [o for o in cu.objs if o.net != NET and (
    (o.rect is not None and o.rect[2] > x0 - 1 and o.rect[0] < x1 + 1 and o.rect[3] > y0 - 1 and o.rect[1] < y1 + 1) or
    (o.rect is None and max(o.a[0], o.b[0]) > x0 - 1 and min(o.a[0], o.b[0]) < x1 + 1 and max(o.a[1], o.b[1]) > y0 - 1 and min(o.a[1], o.b[1]) < y1 + 1))]
LAY = [pcbnew.F_Cu, pcbnew.B_Cu]
nx, ny = int((x1 - x0) / G) + 1, int((y1 - y0) / G) + 1
P = lambda i, j: (x0 + i * G, y0 + j * G)
free = {}


def ok(i, j, l):
    k = (i, j, l)
    if k not in free:
        p = P(i, j)
        free[k] = cu.clear(p, p, WID / 2, NET, {LAY[l]})
    return free[k]


def via_ok(i, j):
    k = (i, j, "v")
    if k not in free:
        p = P(i, j)
        free[k] = cu.hole_ok(p, 0.15) and cu.clear(p, p, 0.3, NET, {pcbnew.F_Cu, pcbnew.B_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu})
    return free[k]


def cell(p):
    return (round((p[0] - x0) / G), round((p[1] - y0) / G))


padA = [p for f in b.GetFootprints() if f.GetReference() == ra for p in f.Pads() if p.GetNumber() == pa][0]
padB = [p for f in b.GetFootprints() if f.GetReference() == rb for p in f.Pads() if p.GetNumber() == pb][0]
startL = [l for l in (0, 1) if padA.IsOnLayer(LAY[l])]
goalL = {l for l in (0, 1) if padB.IsOnLayer(LAY[l])}
sa, sb = cell(A), cell(Bp)
for l in (0, 1):                           # pad cells are always enterable
    free[(sa[0], sa[1], l)] = l in startL
    free[(sb[0], sb[1], l)] = l in goalL
h = lambda i, j: math.hypot(i - sb[0], j - sb[1]) * G
pq = [(h(*sa), 0.0, sa[0], sa[1], l, None) for l in startL]
seen, parent = {}, {}
D8 = [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)]
found = None
while pq:
    f_, g, i, j, l, par = heapq.heappop(pq)
    if (i, j, l) in seen:
        continue
    seen[(i, j, l)] = par
    if (i, j) == sb and l in goalL:
        found = (i, j, l)
        break
    for di, dj in D8:
        ni, nj = i + di, j + dj
        if 0 <= ni < nx and 0 <= nj < ny and (ni, nj, l) not in seen and ok(ni, nj, l):
            c = g + math.hypot(di, dj) * G
            heapq.heappush(pq, (c + h(ni, nj), c, ni, nj, l, (i, j, l)))
    if (i, j, 1 - l) not in seen and via_ok(i, j) and ok(i, j, 1 - l):
        c = g + VIA_COST
        heapq.heappush(pq, (c + h(i, j), c, i, j, 1 - l, (i, j, l)))
if not found:
    sys.exit(f"no path: expanded {len(seen)} nodes, start {sa} layers {startL}, goal {sb} layers {goalL}, grid {nx}x{ny}")
path = []
k = found
while k:
    path.append(k)
    k = seen[k]
path.reverse()
# compress into straight runs per layer, emit tracks + vias
runs, cur = [], [path[0]]
for k in path[1:]:
    if k[2] != cur[-1][2]:
        runs.append(cur); cur = [k]; continue
    if len(cur) >= 2:
        d0 = (cur[-1][0] - cur[-2][0], cur[-1][1] - cur[-2][1]); d1 = (k[0] - cur[-1][0], k[1] - cur[-1][1])
        if d0 != d1:
            runs.append(cur); cur = [cur[-1], k]; continue
    cur.append(k)
runs.append(cur)
pts_layers = []
for r in runs:
    if len(r) >= 2:
        pts_layers.append((P(r[0][0], r[0][1]), P(r[-1][0], r[-1][1]), r[0][2]))
segs = [(A if n == 0 else a, Bp if n == len(pts_layers) - 1 else c, l) for n, (a, c, l) in enumerate(pts_layers)]
# join consecutive segments (layer changes become vias)
for n, (a, c, l) in enumerate(segs):
    if not cu.clear(a, c, WID / 2, NET, {LAY[l]}):
        sys.exit(f"segment {a}->{c} fails exact clearance: {cu.last_hit}")
for n, (a, c, l) in enumerate(segs):
    cu.track(a, c, WID, NET, LAY[l], locked=False)
    if n + 1 < len(segs):
        nxt = segs[n + 1]
        if nxt[2] != l:
            cu.via(c, NET, locked=False)
        if nxt[0] != c:
            cu.track(c, nxt[0], WID, NET, LAY[nxt[2]], locked=False)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(outp, b)
L = sum(math.hypot(c[0] - a[0], c[1] - a[1]) for a, c, _ in segs)
print(f"routed {NET}: {len(segs)} segments, {sum(1 for n in range(len(segs) - 1) if segs[n][2] != segs[n + 1][2])} vias, {L:.1f} mm")
