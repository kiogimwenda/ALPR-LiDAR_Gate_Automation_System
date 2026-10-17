"""Plane fanout: give every SMD pad on a plane net (GND, +3V3) a short stub and a
via to its inner plane, clearance-checked against all other copper.
KiCad python:  python.exe fanout.py in.kicad_pcb out.kicad_pcb"""
import math
import sys

import pcbnew

mm = pcbnew.FromMM
toMM = pcbnew.ToMM
PLANE_NETS = {"GND", "+3V3"}
VIA_D, VIA_DRILL = 0.6, 0.3
CLR = 0.22                      # copper clearance used for checks (rule 0.2)
EDGE = 0.6                      # via centre to board edge
OX, OY, W, H = 100.0, 50.0, 100.0, 90.0
KEEPOUT = (OX + 28 - 0.5, OY + 0.0, OX + 76 + 0.5, OY + 6.4 + 0.5)   # ESP32 antenna

b = pcbnew.LoadBoard(sys.argv[1])


class Obj:
    """Copper obstacle: either a rectangle (pads) or a capsule (tracks/vias)."""
    __slots__ = ("net", "kind", "a", "b", "r", "rect", "layers")

    def __init__(self, net, layers, rect=None, a=None, b=None, r=0.0):
        self.net, self.layers = net, layers
        self.rect, self.a, self.b, self.r = rect, a, b, r


def seg_point_dist(p, a, b):
    ax, ay = a; bx, by = b; px, py = p
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    cx, cy = ax + t * dx, ay + t * dy
    return math.hypot(px - cx, py - cy)


def rect_point_dist(p, rect):
    x0, y0, x1, y1 = rect
    dx = max(x0 - p[0], 0, p[0] - x1)
    dy = max(y0 - p[1], 0, p[1] - y1)
    return math.hypot(dx, dy)


def seg_seg_dist(a, b, c, d):
    # sample-free exact-enough: min of endpoint-to-segment distances (+ intersection test)
    def ccw(p, q, r):
        return (r[1] - p[1]) * (q[0] - p[0]) > (q[1] - p[1]) * (r[0] - p[0])
    if ccw(a, c, d) != ccw(b, c, d) and ccw(a, b, c) != ccw(a, b, d):
        return 0.0
    return min(seg_point_dist(a, c, d), seg_point_dist(b, c, d), seg_point_dist(c, a, b), seg_point_dist(d, a, b))


def seg_rect_dist(a, b, rect):
    x0, y0, x1, y1 = rect
    if rect_point_dist(a, rect) == 0 or rect_point_dist(b, rect) == 0:
        return 0.0
    edges = [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]
    return min(seg_seg_dist(a, b, e0, e1) for e0, e1 in edges)


obst = []
CU = {"F": pcbnew.F_Cu, "B": pcbnew.B_Cu, "1": pcbnew.In1_Cu, "2": pcbnew.In2_Cu}
ALL = set(CU.values())
for f in b.GetFootprints():
    for p in f.Pads():
        bb = p.GetBoundingBox()
        rect = (toMM(bb.GetX()), toMM(bb.GetY()), toMM(bb.GetRight()), toMM(bb.GetBottom()))
        layers = {l for l in ALL if p.IsOnLayer(l)} if p.HasHole() else {pcbnew.F_Cu}
        if p.HasHole():
            layers = set(ALL)            # drill goes through every layer
        obst.append(Obj(p.GetNetname(), layers, rect=rect))
for t in b.GetTracks():
    if t.GetClass() == "PCB_VIA":
        c = (toMM(t.GetPosition().x), toMM(t.GetPosition().y))
        obst.append(Obj(t.GetNetname(), set(ALL), a=c, b=c, r=toMM(t.GetWidth(pcbnew.F_Cu)) / 2))
    else:
        obst.append(Obj(t.GetNetname(), {t.GetLayer()},
                        a=(toMM(t.GetStart().x), toMM(t.GetStart().y)), b=(toMM(t.GetEnd().x), toMM(t.GetEnd().y)),
                        r=toMM(t.GetWidth()) / 2))
# NPTH holes / footprint keepouts are covered by pad rects (NPTH pads have no net -> "" net)


def clear_capsule(a, bpt, r, net, layers):
    for o in obst:
        if o.net == net and net != "":
            continue
        if not (o.layers & layers):
            continue
        if o.rect is not None:
            d = seg_rect_dist(a, bpt, o.rect)
        else:
            d = seg_seg_dist(a, bpt, o.a, o.b) - o.r
        if d - r < CLR:
            return False
    return True


def in_board(p):
    x, y = p
    if not (OX + EDGE <= x <= OX + W - EDGE and OY + EDGE <= y <= OY + H - EDGE):
        return False
    if KEEPOUT[0] <= x <= KEEPOUT[2] and KEEPOUT[1] <= y <= KEEPOUT[3]:
        return False
    return True


def add_track(a, bpt, w, net):
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(pcbnew.VECTOR2I(mm(a[0]), mm(a[1]))); t.SetEnd(pcbnew.VECTOR2I(mm(bpt[0]), mm(bpt[1])))
    t.SetWidth(mm(w)); t.SetLayer(pcbnew.F_Cu); t.SetNet(net); t.SetLocked(True)
    b.Add(t)
    obst.append(Obj(net.GetNetname(), {pcbnew.F_Cu}, a=a, b=bpt, r=w / 2))


# drilled holes (vias + plated pads): keep >= 0.25 mm hole-to-hole whatever the net
holes = []
for f in b.GetFootprints():
    for p in f.Pads():
        if p.HasHole():
            q = p.GetPosition()
            dr = max(toMM(p.GetDrillSize().x), toMM(p.GetDrillSize().y))
            holes.append((toMM(q.x), toMM(q.y), dr / 2 + VIA_DRILL / 2 + 0.3))


def add_via(c, net):
    holes.append((c[0], c[1], VIA_DRILL + 0.3))
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(pcbnew.VECTOR2I(mm(c[0]), mm(c[1])))
    v.SetWidth(mm(VIA_D)); v.SetDrill(mm(VIA_DRILL)); v.SetNet(net); v.SetLocked(True)
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    b.Add(v)
    obst.append(Obj(net.GetNetname(), set(ALL), a=c, b=c, r=VIA_D / 2))


done, failed = 0, []
pads = [(f, p) for f in b.GetFootprints() for p in f.Pads()
        if p.GetNetname() in PLANE_NETS and not p.HasHole()]
# fine-pitch IC pins first: they have the fewest options
pads.sort(key=lambda fp: min(toMM(fp[1].GetSize().x), toMM(fp[1].GetSize().y)))
def has_thermal_vias(f, p):
    """Exposed pads (ESP32 EPAD, PowerPAD) already reach the plane through
    the footprint's own plated holes."""
    return any(q.HasHole() and q.GetNetname() == p.GetNetname() and p.HitTest(q.GetPosition())
               for q in f.Pads())


HAND = [t for t in b.GetTracks() if not isinstance(t, pcbnew.PCB_VIA)]


def already_routed(p):
    """Pads the hand-routing step already tied to a plane via."""
    return any(t.GetNetCode() == p.GetNetCode() and (p.HitTest(t.GetStart()) or p.HitTest(t.GetEnd())) for t in HAND)


for f, p in pads:
    if has_thermal_vias(f, p) or (f.GetReference() == "U5" and already_routed(p)):
        continue
    net = p.GetNet(); netname = p.GetNetname()
    pc = (toMM(p.GetPosition().x), toMM(p.GetPosition().y))
    fc = (toMM(f.GetPosition().x), toMM(f.GetPosition().y))
    bb = p.GetBoundingBox()
    hw, hh = toMM(bb.GetWidth()) / 2, toMM(bb.GetHeight()) / 2
    w = 0.3 if min(hw, hh) * 2 >= 0.5 else 0.25
    # preferred direction: away from the footprint centre, then the others
    out = (pc[0] - fc[0], pc[1] - fc[1])
    dirs = [(1, 0), (-1, 0), (0, 1), (0, -1), (0.7071, 0.7071), (0.7071, -0.7071), (-0.7071, 0.7071), (-0.7071, -0.7071)]
    dirs.sort(key=lambda d: -(d[0] * out[0] + d[1] * out[1]))
    ok = False
    for extra in (0.0, 0.3, 0.7, 1.2, 1.8):
        for d in dirs:
            reach = abs(d[0]) * hw + abs(d[1]) * hh
            dist = reach + CLR + VIA_D / 2 + extra + 0.1
            c = (pc[0] + d[0] * dist, pc[1] + d[1] * dist)
            if not in_board(c):
                continue
            if not clear_capsule(c, c, VIA_D / 2, netname, set(ALL)):
                continue
            if any(math.hypot(c[0] - h[0], c[1] - h[1]) < h[1 + 1] for h in holes):
                continue
            if not clear_capsule(pc, c, w / 2, netname, {pcbnew.F_Cu}):
                continue
            add_track(pc, c, w, net)
            add_via(c, net)
            ok = True
            done += 1
            break
        if ok:
            break
    if not ok:
        failed.append(f"{f.GetReference()}.{p.GetNumber()}")
print(f"fanout: {done} pads got a plane via; failed: {' '.join(failed) or 'none'}")
pcbnew.SaveBoard(sys.argv[2], b)
