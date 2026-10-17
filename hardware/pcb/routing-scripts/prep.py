"""Hand placement tweaks + hand routes for the critical nets, before autorouting.
python.exe prep.py in.kicad_pcb out.kicad_pcb

1. Crystal network (guide 6.4: short, top layer, no vias): Y1 west of the W5500
   XI/XO pins, R22 inline on XO, C27/C28 at the crystal.
2. W5500 west-side pins that would wall the crystal in (LEDs, 3V3, GND, SPI)
   escape inward to vias under the IC body (LQFP-48, no exposed pad).
3. Buck switch node (TPS5430 PH -> L1 / D3 / C3) as a solid F.Cu pour.
"""
import sys

import pcbnew

import math

from geo import B, CU_ALL, Copper, Obj, mm, pad_xy, toMM

b = pcbnew.LoadBoard(sys.argv[1])
FP = {f.GetReference(): f for f in b.GetFootprints()}


def place(ref, x, y, rot):
    f = FP[ref]
    f.SetOrientationDegrees(0)
    f.SetPosition(pcbnew.VECTOR2I(mm(B(x, y)[0]), mm(B(x, y)[1])))
    f.SetOrientationDegrees(rot)


# ---- 1. re-place the crystal group (board coordinates, mm)
place("Y1", 70.8, 35.825, 270)     # pin1 XI top-left, pin3 bottom-right
place("R22", 75.0, 36.1, 90)       # pin2 (XO) up, pin1 (to crystal) down
place("C27", 69.8, 32.4, 180)      # pin1 (XI) right
place("C28", 73.28, 39.55, 270)    # pin1 (crystal side) up
place("R21", 73.4, 34.425, 270)   # 1M bias: pin1 (XI) on the XI line, pin2 (XO) on the XO line
place("C21", 75.6, 42.2, 90)       # W5500 3V3 decoupling, freed from the crystal spot
place("D3", 30.5, 44.5, 180)       # catch diode cathode toward the switch node
# W5500 top-edge caps in the same left->right order as their pins (23 GND, 22 1V2O,
# 20 TOCAP, 19 GND) so every pin reaches its cap without crossing
place("C26", 77.9, 28.45, 180)     # pin2 GND left, pin1 1V2O right
place("C25", 81.25, 28.45, 0)      # pin1 TOCAP left, pin2 GND right

cu = Copper(b)
P = lambda ref, n: pad_xy(b, ref, n)
XI, XO, XC = "/Ethernet W5500 + RJ45/XI", "/Ethernet W5500 + RJ45/XO", "Net-(C28-Pad1)"
W = 0.2
u30, u31 = P("U5", 30), P("U5", 31)
y1_1, y1_3 = P("Y1", 1), P("Y1", 3)
c27_1, c28_1 = P("C27", 1), P("C28", 1)
r22_xo, r22_xc = P("R22", 2), P("R22", 1)
top = B(0, 33.6)[1]                                       # XI runs over the top of Y1
r21_xi, r21_xo = P("R21", 1), P("R21", 2)
cu.path([u30, (u30[0] - 1.0, u30[1]), (u30[0] - 2.15, top), (r21_xi[0], top), (y1_1[0], top), y1_1], W, XI)
cu.path([(c27_1[0], top), c27_1], W, XI)
cu.path([u31, r22_xo, r21_xo], W, XO)
cu.path([r22_xc, y1_3], W, XC)
cu.path([c28_1, (c28_1[0], y1_3[1])], W, XC)
cu.path([P("Y1", 4), P("Y1", 2)], 0.25, "GND")          # GND link under the crystal

# ---- 2. W5500 west pins escape inward to vias under the body
ESC = {25: "LINKLED", 27: "/Ethernet W5500 + RJ45/ACTLED", 28: "+3V3", 29: "GND",
       32: "W5500_CS", 33: "Net-(U5-SCLK)", 34: "W5500_MISO", 35: "Net-(U5-MOSI)", 36: "W5500_INT"}
u5 = FP["U5"]
cx, cy = toMM(u5.GetPosition().x), toMM(u5.GetPosition().y)
# Order-preserving fan: the 9 pins (0.5 mm pitch) spread to a 0.7125 mm pitch
# over 1.7 mm, then run straight to vias in two staggered columns under the body.
pins = sorted(ESC, key=lambda n: P("U5", n)[1])
y0, y1 = cy - 2.25, cy + 2.9          # top 0.7 mm left free for the AVDD bus
step = (y1 - y0) / (len(pins) - 1)
failed = []
for i, pin in enumerate(pins):
    net, p = ESC[pin], P("U5", pin)
    vy = y0 + step * i
    vx = cx - 0.8 if i % 2 == 0 else cx + 0.5
    pts = [p, (cx - 3.1, p[1]), (cx - 1.4, vy), (vx, vy)]
    try:
        cu.path(pts, W, net)
        cu.via((vx, vy), net)
    except RuntimeError as e:
        failed.append((pin, str(e)[-90:]))
print("W5500 inward escapes failed for pins:", failed or "none")

# ---- 2b. W5500 AVDD: bus just inside the top pins + strap inside the right pins
AV = "/Ethernet W5500 + RJ45/AVDD"
bus_y, strap_x = cy - 2.95, cx + 2.7
top_av = [P("U5", n) for n in (21, 17, 15)]
right_av = [P("U5", n) for n in (11, 8, 4)]
for p in top_av:
    cu.path([p, (p[0], bus_y)], 0.25, AV)
cu.path([(top_av[0][0], bus_y), (strap_x, bus_y), (strap_x, right_av[-1][1])], 0.25, AV)
for p in right_av:
    cu.path([p, (strap_x, p[1])], 0.25, AV)
# out through pin 4 to the AVDD caps / ferrite, chained on the top layer
p4, c23, c24, fb1 = P("U5", 4), P("C23", 1), P("C24", 1), P("FB1", 2)
# straight east (no diagonal: the RX pair above must stay free to turn north),
# then split at the cap column: up to C23, down to C24 and the ferrite
cu.path([p4, (c23[0], p4[1]), c23], 0.25, AV)
cu.path([(c23[0], p4[1]), (c23[0], c24[1] - 0.6), c24, (c24[0], fb1[1] - 0.6), fb1], 0.25, AV)

# ---- 2c. W5500 top edge -> C26 / C25 (GND pins 19, 16, 14 share C25's GND pad)
cu.path([P("U5", 23), (P("U5", 23)[0], P("U5", 23)[1] - 0.9), P("C26", 2)], 0.25, "GND")
cu.path([P("U5", 22), (P("U5", 22)[0], P("U5", 22)[1] - 0.9), P("C26", 1)], 0.2, "Net-(U5-1V2O)")
cu.path([P("U5", 20), P("C25", 1)], 0.2, "Net-(U5-TOCAP)")
for n in (19, 16, 14):
    q = P("U5", n)
    cu.path([q, (q[0], q[1] - 0.9), P("C25", 2)], 0.25, "GND")

# ---- 3. buck: BOOT strap first (the switch-node pour flows around it), then the PH pour
boot = "Net-(U1-BOOT)"
u1_1, c3_boot = P("U1", 1), P("C3", 1)
cu.path([u1_1, (u1_1[0] - 0.9, u1_1[1] + 0.9), (c3_boot[0], c3_boot[1] - 0.0)], 0.3, boot, check=False)
PH = b.FindNet("Net-(D3-K)")
z = pcbnew.ZONE(b)
z.SetLayer(pcbnew.F_Cu); z.SetNet(PH); z.SetAssignedPriority(10)
z.SetLocalClearance(mm(0.25)); z.SetMinThickness(mm(0.25))
z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
ol = z.Outline(); ol.NewOutline()
for x, y in [(29.4, 39.45), (31.4, 39.45), (33.0, 41.0), (37.4, 41.0), (37.4, 45.7), (31.0, 45.7), (31.0, 42.6), (29.4, 40.9)]:
    ax, ay = B(x, y); ol.Append(mm(ax), mm(ay))
b.Add(z)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(sys.argv[2], b)
print("prep done")
