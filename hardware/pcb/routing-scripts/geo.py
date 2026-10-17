"""Shared copper-geometry helpers for the hand-routing scripts (KiCad python).
All coordinates in mm, KiCad absolute (board origin is at OX, OY)."""
import math

import pcbnew

mm = pcbnew.FromMM
toMM = pcbnew.ToMM
OX, OY, W, H = 100.0, 50.0, 100.0, 90.0
CU_ALL = {pcbnew.F_Cu, pcbnew.B_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu}
PLANES = {"GND": pcbnew.In1_Cu, "+3V3": pcbnew.In2_Cu}


def B(x, y):
    """Board-relative mm -> KiCad absolute mm."""
    return (OX + x, OY + y)


class Obj:
    __slots__ = ("net", "layers", "rect", "a", "b", "r")

    def __init__(self, net, layers, rect=None, a=None, b=None, r=0.0):
        self.net, self.layers, self.rect, self.a, self.b, self.r = net, layers, rect, a, b, r


def seg_point(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy)


def rect_point(p, r):
    return math.hypot(max(r[0] - p[0], 0, p[0] - r[2]), max(r[1] - p[1], 0, p[1] - r[3]))


def _ccw(p, q, r):
    return (r[1] - p[1]) * (q[0] - p[0]) > (q[1] - p[1]) * (r[0] - p[0])


def seg_seg(a, b, c, d):
    if _ccw(a, c, d) != _ccw(b, c, d) and _ccw(a, b, c) != _ccw(a, b, d):
        return 0.0
    return min(seg_point(a, c, d), seg_point(b, c, d), seg_point(c, a, b), seg_point(d, a, b))


def seg_rect(a, b, r):
    if rect_point(a, r) == 0 or rect_point(b, r) == 0:
        return 0.0
    x0, y0, x1, y1 = r
    return min(seg_seg(a, b, e0, e1) for e0, e1 in
               (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))))


class Copper:
    """Snapshot of board copper for clearance checks while adding items."""

    def __init__(self, board, clr=0.22):
        self.b, self.clr, self.objs, self.holes = board, clr, [], []
        for f in board.GetFootprints():
            for p in f.Pads():
                bb = p.GetBoundingBox()
                rect = (toMM(bb.GetX()), toMM(bb.GetY()), toMM(bb.GetRight()), toMM(bb.GetBottom()))
                layers = set(CU_ALL) if p.HasHole() else {l for l in (pcbnew.F_Cu, pcbnew.B_Cu) if p.IsOnLayer(l)}
                self.objs.append(Obj(p.GetNetname(), layers, rect=rect))
                if p.HasHole():
                    q = p.GetPosition()
                    self.holes.append((toMM(q.x), toMM(q.y), max(toMM(p.GetDrillSize().x), toMM(p.GetDrillSize().y)) / 2))
        for t in board.GetTracks():
            self._add_item(t)

    def _add_item(self, t):
        if isinstance(t, pcbnew.PCB_VIA):
            c = (toMM(t.GetPosition().x), toMM(t.GetPosition().y))
            self.objs.append(Obj(t.GetNetname(), set(CU_ALL), a=c, b=c, r=toMM(t.GetWidth(pcbnew.F_Cu)) / 2))
            self.holes.append((c[0], c[1], toMM(t.GetDrillValue()) / 2))
        else:
            self.objs.append(Obj(t.GetNetname(), {t.GetLayer()},
                                 a=(toMM(t.GetStart().x), toMM(t.GetStart().y)),
                                 b=(toMM(t.GetEnd().x), toMM(t.GetEnd().y)), r=toMM(t.GetWidth()) / 2))

    def clear(self, a, b, r, net, layers, ignore=()):
        for o in self.objs:
            if (o.net == net and net != "") or not (o.layers & layers) or o in ignore:
                continue
            d = seg_rect(a, b, o.rect) if o.rect is not None else seg_seg(a, b, o.a, o.b) - o.r
            if d - r < self.clr:
                self.last_hit = (o.net, o.rect, o.a, o.b, round(d - r, 3))
                return False
        return True

    def hole_ok(self, c, drill_r, gap=0.3):
        return all(math.hypot(c[0] - x, c[1] - y) >= drill_r + hr + gap for x, y, hr in self.holes)

    def track(self, a, b, w, netname, layer=pcbnew.F_Cu, locked=True):
        t = pcbnew.PCB_TRACK(self.b)
        t.SetStart(pcbnew.VECTOR2I(mm(a[0]), mm(a[1]))); t.SetEnd(pcbnew.VECTOR2I(mm(b[0]), mm(b[1])))
        t.SetWidth(mm(w)); t.SetLayer(layer); t.SetNet(self.b.FindNet(netname)); t.SetLocked(locked)
        self.b.Add(t); self._add_item(t)
        return t

    def via(self, c, netname, d=0.6, drill=0.3, locked=True):
        v = pcbnew.PCB_VIA(self.b)
        v.SetPosition(pcbnew.VECTOR2I(mm(c[0]), mm(c[1])))
        v.SetWidth(mm(d)); v.SetDrill(mm(drill)); v.SetNet(self.b.FindNet(netname)); v.SetLocked(locked)
        v.SetViaType(pcbnew.VIATYPE_THROUGH)
        self.b.Add(v); self._add_item(v)
        return v

    def path(self, pts, w, netname, layer=pcbnew.F_Cu, check=True):
        """Add a polyline; raise if any segment violates clearance."""
        for a, b in zip(pts, pts[1:]):
            if check and not self.clear(a, b, w / 2, netname, {layer}):
                raise RuntimeError(f"{netname}: segment {a}->{b} violates clearance: {self.last_hit}")
        for a, b in zip(pts, pts[1:]):
            self.track(a, b, w, netname, layer)


def pad_xy(board, ref, num):
    for f in board.GetFootprints():
        if f.GetReference() == ref:
            for p in f.Pads():
                if p.GetNumber() == str(num):
                    q = p.GetPosition()
                    return (toMM(q.x), toMM(q.y))
    raise KeyError(f"{ref}.{num}")
