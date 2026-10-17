"""Guide 6.6 finishing: GND fills on F.Cu/B.Cu, solid connection for the ESP32 and
TPS5430 thermal pads, GND via stitching on a 5 mm grid.
python.exe finish.py in.kicad_pcb out.kicad_pcb"""
import sys

import pcbnew

from geo import OX, OY, W, H, Copper, mm, toMM

b = pcbnew.LoadBoard(sys.argv[1])
GND = b.FindNet("GND")

# ---- thermal pads: direct (solid) connection to the planes / fills
for f in b.GetFootprints():
    for ref, num in (("U3", "41"), ("U1", "9"), ("J6", "SH"), ("J4", "1")):
        if f.GetReference() == ref:
            for p in f.Pads():
                if p.GetNumber() == num:
                    p.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)

# ---- references crowded out by the hand re-placement: keep them on F.Fab only
for f in b.GetFootprints():
    if f.GetReference() in ("C21", "C27", "R21", "R23", "U5", "Y1"):
        f.Reference().SetLayer(pcbnew.F_Fab)

# ---- GND fills on the outer layers (lowest priority; the PH pour wins)
for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(b)
    z.SetLayer(layer); z.SetNet(GND); z.SetAssignedPriority(0)
    z.SetLocalClearance(mm(0.3)); z.SetMinThickness(mm(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)   # SMD solid, THT thermal
    z.SetThermalReliefGap(mm(0.5)); z.SetThermalReliefSpokeWidth(mm(0.5))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)   # no floating copper at all
    ol = z.Outline(); ol.NewOutline()
    for x, y in [(0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (0.5, H - 0.5)]:
        ol.Append(mm(OX + x), mm(OY + y))
    b.Add(z)
filler = pcbnew.ZONE_FILLER(b)
filler.Fill(b.Zones())

# ---- stitching: 5 mm grid, only where both outer fills exist and nothing else is near
fills = [z for z in b.Zones() if z.GetNetname() == "GND" and z.GetLayer() in (pcbnew.F_Cu, pcbnew.B_Cu)]
court = []
for f in b.GetFootprints():
    f.BuildCourtyardCaches()
    for side in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
        c = f.GetCourtyard(side)
        if c.OutlineCount():
            bb = c.BBox()
            court.append((toMM(bb.GetX()), toMM(bb.GetY()), toMM(bb.GetRight()), toMM(bb.GetBottom())))
cu = Copper(b, clr=0.3)
# relay contacts carry their own 0.6 mm clearance (net class RelayContact)
relay = Copper(b, clr=0.65)
relay.objs = [o for o in relay.objs if "/Relay Outputs (4 channels)/" in o.net and o.net.split("/")[-1][:2] in ("CO", "NO", "NC")]
KEEP = (OX + 27.5, OY - 1, OX + 76.5, OY + 7.0)            # ESP32 antenna keep-out
added = 0
for i in range(int(W / 5) + 1):
    for j in range(int(H / 5) + 1):
        x, y = OX + 2.5 + 5 * i, OY + 2.5 + 5 * j
        if not (OX + 2 <= x <= OX + W - 2 and OY + 2 <= y <= OY + H - 2):
            continue
        if KEEP[0] <= x <= KEEP[2] and KEEP[1] <= y <= KEEP[3]:
            continue
        if any(c[0] - 0.3 <= x <= c[2] + 0.3 and c[1] - 0.3 <= y <= c[3] + 0.3 for c in court):
            continue
        pt = pcbnew.VECTOR2I(mm(x), mm(y))
        if not all(z.HitTestFilledArea(z.GetLayer(), pt) for z in fills):
            continue
        if not cu.hole_ok((x, y), 0.15) or not cu.clear((x, y), (x, y), 0.3, "GND", {pcbnew.F_Cu, pcbnew.B_Cu}) \
                or not relay.clear((x, y), (x, y), 0.3, "GND", {pcbnew.F_Cu, pcbnew.B_Cu}):
            continue
        cu.via((x, y), "GND")
        added += 1
filler.Fill(b.Zones())
pcbnew.SaveBoard(sys.argv[2], b)
print("stitching vias:", added)
