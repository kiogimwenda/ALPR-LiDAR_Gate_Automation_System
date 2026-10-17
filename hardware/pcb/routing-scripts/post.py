"""Import a Freerouting session, delete dangling copper, fill zones.
python.exe post.py in.kicad_pcb in.ses out.kicad_pcb"""
import sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1])
assert pcbnew.ImportSpecctraSES(b, sys.argv[2])
PLANES = {"GND": pcbnew.In1_Cu, "+3V3": pcbnew.In2_Cu}
pads = [p for f in b.GetFootprints() for p in f.Pads()]


def touches(pt, item, layer):
    t = item
    if isinstance(t, pcbnew.PCB_VIA):
        return t.HitTest(pt)
    return t.GetLayer() == layer and t.HitTest(pt)


removed = 0
for allow_locked in (False, True):
  while True:
      tracks = list(b.GetTracks())
      dead = []
      for t in tracks:
          net = t.GetNetCode()
          same = [o for o in tracks if o is not t and o.GetNetCode() == net]
          npads = [p for p in pads if p.GetNetCode() == net]
          if isinstance(t, pcbnew.PCB_VIA):
              pt = t.GetPosition()
              layers = {PLANES[t.GetNetname()]} if t.GetNetname() in PLANES else set()
              for p in npads:
                  if p.HitTest(pt):
                      layers |= {pcbnew.F_Cu, pcbnew.B_Cu} if p.HasHole() else {pcbnew.F_Cu}
              layers |= {o.GetLayer() for o in same if not isinstance(o, pcbnew.PCB_VIA) and o.HitTest(pt)}
              if len(layers) < 2:
                  dead.append(t)
              continue
          for end in (t.GetStart(), t.GetEnd()):
              lay = t.GetLayer()
              ok = any(p.HitTest(end) and p.IsOnLayer(lay) for p in npads) or any(touches(end, o, lay) for o in same)
              if not ok:
                  dead.append(t)
                  break
      if not dead:
          break
      dead = [t for t in dead if allow_locked or not t.IsLocked()]
      if not dead:
          break
      for t in dead:
          b.Delete(t)
      removed += len(dead)
# Freerouting necks traces down to 0.15 mm at some pads; the spec minimum is 0.2 mm
MIN_W = pcbnew.FromMM(0.2)
widened = 0
for t in b.GetTracks():
    if not isinstance(t, pcbnew.PCB_VIA) and t.GetWidth() < MIN_W:
        t.SetWidth(MIN_W)
        widened += 1
print("widened", widened, "segments to 0.2 mm")

# Fine-pitch pads: a trace leaving a pad off its centreline (Freerouting does
# this after necking down) loses clearance to the next pin once widened to
# 0.2 mm. Snap such straight exits, and the vertex they share, onto the centreline.
snapped = 0
for p in pads:
    sz = p.GetSize()
    if p.HasHole() or min(sz.x, sz.y) > pcbnew.FromMM(0.35):
        continue
    bb = p.GetBoundingBox(); c = p.GetPosition()
    horiz = bb.GetWidth() > bb.GetHeight()
    for t in list(b.GetTracks()):
        if isinstance(t, pcbnew.PCB_VIA) or t.GetNetCode() != p.GetNetCode():
            continue
        s0, e0 = t.GetStart(), t.GetEnd()
        inside, outside = (s0, e0) if p.HitTest(s0) else ((e0, s0) if p.HitTest(e0) else (None, None))
        if inside is None:
            continue
        if horiz and s0.y == e0.y and s0.y != c.y:
            old, new_y = outside, c.y
            moved = pcbnew.VECTOR2I(old.x, new_y)
            for o in b.GetTracks():
                if not isinstance(o, pcbnew.PCB_VIA) and o.GetNetCode() == t.GetNetCode():
                    if o.GetStart() == old: o.SetStart(moved)
                    if o.GetEnd() == old: o.SetEnd(moved)
            if t.GetStart() == inside: t.SetStart(pcbnew.VECTOR2I(inside.x, new_y))
            else: t.SetEnd(pcbnew.VECTOR2I(inside.x, new_y))
            snapped += 1
        elif not horiz and s0.x == e0.x and s0.x != c.x:
            old, new_x = outside, c.x
            moved = pcbnew.VECTOR2I(new_x, old.y)
            for o in b.GetTracks():
                if not isinstance(o, pcbnew.PCB_VIA) and o.GetNetCode() == t.GetNetCode():
                    if o.GetStart() == old: o.SetStart(moved)
                    if o.GetEnd() == old: o.SetEnd(moved)
            if t.GetStart() == inside: t.SetStart(pcbnew.VECTOR2I(new_x, inside.y))
            else: t.SetEnd(pcbnew.VECTOR2I(new_x, inside.y))
            snapped += 1
print("snapped", snapped, "pad exits onto centrelines")
# segments lying entirely inside one of their own pads are redundant (snap leftovers)
inside_pad = [t for t in b.GetTracks() if not isinstance(t, pcbnew.PCB_VIA) and not t.IsLocked()
              and any(p.GetNetCode() == t.GetNetCode() and p.HitTest(t.GetStart()) and p.HitTest(t.GetEnd()) for p in pads)]
for t in inside_pad:
    b.Delete(t)
print("removed", len(inside_pad), "segments buried inside their own pads")
# Locked plane vias from the fanout: if an autorouted track ended up closer than
# 0.2 mm, slide the via (and its stub end) to the nearby spot with most clearance.
import math
mmf = pcbnew.ToMM


def dseg(p, a, c):
    dx, dy = c[0] - a[0], c[1] - a[1]; L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy)


segs, rects = [], []
for t in b.GetTracks():
    if isinstance(t, pcbnew.PCB_VIA):
        q = (mmf(t.GetPosition().x), mmf(t.GetPosition().y)); segs.append((q, q, mmf(t.GetWidth(pcbnew.F_Cu)) / 2, t.GetNetname(), None))
    else:
        segs.append(((mmf(t.GetStart().x), mmf(t.GetStart().y)), (mmf(t.GetEnd().x), mmf(t.GetEnd().y)), mmf(t.GetWidth()) / 2, t.GetNetname(), t.GetLayer()))
for p in pads:
    bb = p.GetBoundingBox()
    rects.append((mmf(bb.GetX()), mmf(bb.GetY()), mmf(bb.GetRight()), mmf(bb.GetBottom()), p.GetNetname()))


def clearance(c, net):
    m = min((dseg(c, a, e) - r - 0.3 for a, e, r, n, _ in segs if n != net and (a != c or e != c)), default=9)
    m2 = min((math.hypot(max(x0 - c[0], 0, c[0] - x1), max(y0 - c[1], 0, c[1] - y1)) - 0.3 for x0, y0, x1, y1, n in rects if n != net), default=9)
    return min(m, m2)


nudged = 0
for v in [t for t in b.GetTracks() if isinstance(t, pcbnew.PCB_VIA) and t.IsLocked() and t.GetNetname() in PLANES]:
    c0 = (mmf(v.GetPosition().x), mmf(v.GetPosition().y))
    if clearance(c0, v.GetNetname()) >= 0.205:
        continue
    stub = [t for t in b.GetTracks() if t.GetNetCode() == v.GetNetCode() and not isinstance(t, pcbnew.PCB_VIA)
            and v.GetPosition() in (t.GetStart(), t.GetEnd())]
    if len(stub) != 1:
        continue
    far = stub[0].GetStart() if stub[0].GetEnd() == v.GetPosition() else stub[0].GetEnd()
    fx, fy = mmf(far.x), mmf(far.y); L = math.hypot(c0[0] - fx, c0[1] - fy) or 1
    u = ((c0[0] - fx) / L, (c0[1] - fy) / L)
    best = max(((clearance((c0[0] + u[0] * k * 0.01, c0[1] + u[1] * k * 0.01), v.GetNetname()),
                 (c0[0] + u[0] * k * 0.01, c0[1] + u[1] * k * 0.01)) for k in range(-20, 31)), key=lambda r: r[0])
    if best[0] < 0.205:
        print("could not clear via at", c0, "best", round(best[0], 3)); continue
    nv = pcbnew.VECTOR2I(pcbnew.FromMM(best[1][0]), pcbnew.FromMM(best[1][1]))
    for t in b.GetTracks():
        if t.GetNetCode() == v.GetNetCode() and not isinstance(t, pcbnew.PCB_VIA):
            if t.GetEnd() == v.GetPosition(): t.SetEnd(nv)
            if t.GetStart() == v.GetPosition(): t.SetStart(nv)
    v.SetPosition(nv); nudged += 1
print("nudged", nudged, "plane vias")
filler = pcbnew.ZONE_FILLER(b)
filler.Fill(b.Zones())
nt = sum(1 for t in b.GetTracks() if not isinstance(t, pcbnew.PCB_VIA))
nv = sum(1 for t in b.GetTracks() if isinstance(t, pcbnew.PCB_VIA))
print(f"imported: {nt} track segments, {nv} vias; removed {removed} dangling items")
pcbnew.SaveBoard(sys.argv[3], b)
