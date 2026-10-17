"""Placement fixes + complete silkscreen legend on the unrouted placement.
python.exe relabel.py in.kicad_pcb out.kicad_pcb

* J1 / J9 / J10 (MKDS screw terminals) turned so the wire entries face out of
  the board edge (3D model check: the entry side is footprint +y).
* RESET / BOOT moved next to the USB-C port: one programming corner.
* Every connector named, every pin labelled (board coordinates below)."""
import sys

import pcbnew

from geo import B, mm

b = pcbnew.LoadBoard(sys.argv[1])
FP = {f.GetReference(): f for f in b.GetFootprints()}


def place(ref, x, y, rot):
    f = FP[ref]
    f.SetOrientationDegrees(0)
    f.SetPosition(pcbnew.VECTOR2I(mm(B(x, y)[0]), mm(B(x, y)[1])))
    f.SetOrientationDegrees(rot)


place("J1", 5.54, 34.96, 270)      # entry -> left edge;  pin1 (+12V) top, pin2 (GND) below
place("J10", 94.46, 57.16, 90)     # entry -> right edge; pin1 A bottom, B, GND top
place("J9", 94.46, 77.15, 90)      # entry -> right edge; pin1 +12V bottom, GND top
place("SW2", 6.5, 21.0, 0)         # BOOT  } next to the USB-C port
place("SW1", 17.0, 21.0, 0)        # RESET }
place("R30", 86.0, 7.0, 0)         # make room for the JTAG pin legend
place("C1", 17.6, 32.0, 180)       # +0.4 mm so J1 can sit 0.4 mm in from the edge

# designators of the moved parts go into the legend text instead
for r in ("J1", "J9", "J10", "SW1", "SW2", "J5", "JP1", "TP1", "TP2", "TP3", "TP4", "D61", "D62", "D63", "D64"):
    FP[r].Reference().SetLayer(pcbnew.F_Fab)

# ---- replace every front-silk board text with the full legend
for d in list(b.GetDrawings()):
    if isinstance(d, pcbnew.PCB_TEXT) and d.GetLayer() == pcbnew.F_SilkS:
        b.Delete(d)


def text(s, x, y, size=0.8, bold=False, rot=0):
    t = pcbnew.PCB_TEXT(b)
    t.SetText(s)
    ax, ay = B(x, y)
    t.SetPosition(pcbnew.VECTOR2I(mm(ax), mm(ay)))
    t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(size * (0.2 if bold else 0.15)))
    t.SetTextAngleDegrees(rot)
    b.Add(t)


# J1 12 V input (left edge)
text("J1 12V IN", 5.4, 30.6, 1.0, bold=True)
text("+", 11.75, 34.96, 1.5, bold=True)
text("-", 11.75, 40.04, 1.5, bold=True)

# programming corner
text("RESET", 17.0, 25.1)
text("BOOT", 6.5, 25.1)
text("USB", 25.7, 3.0)
text("PWR", 36.8, 18.8)

# J4 UART0 (vertical header, pin 1 at top)
text("J4 UART0", 65.55, 6.9, bold=True)
for i, s in enumerate(["GND", "3V3", "TX", "RX"]):
    text(s, 69.4, 9.05 + 2.54 * i)

# J5 JTAG (horizontal along the top edge, pin 1 at left)
text("J5 JTAG", 72.9, 3.05, bold=True)
for i, s in enumerate(["GND", "TCK", "TDO", "TDI", "TMS"]):
    text(s, 78.05 + 2.54 * i, 5.45)
text("LAN", 91.0, 6.6, bold=True)

# J7 relay outputs: channel function row + per-pin row (firmware: CH1 drive open, CH2 drive close)
J7X, J8X, PIT = 11.55, 61.05, 3.81
for ch, name in enumerate(["CH1 OPEN", "CH2 CLOSE", "CH3 SPARE", "CH4 SPARE"]):
    text(name, J7X + PIT * (3 * ch + 1), 77.0)
for i in range(12):
    text(["COM", "NO", "NC"][i % 3], J7X + PIT * i, 78.35)
# J8 field inputs
for k, name in enumerate(["OPEN LIM", "CLOSE LIM", "BEAM"]):
    text(name, J8X + PIT * (2 * k + 0.5), 77.0)
for i in range(6):
    text(["SIG", "GND"][i % 2], J8X + PIT * i, 78.35)

# J9 safety-beam supply (fused 12 V), J10 RS-485
text("J9 BEAM 12V", 95.3, 67.6, bold=True)
text("+", 87.9, 77.15, 1.5, bold=True)
text("-", 87.9, 72.07, 1.5, bold=True)
text("J10 RS-485", 94.4, 42.6, bold=True)
for y, s in ((57.16, "A"), (52.08, "B"), (47.0, "G")):
    text(s, 88.4, y, 0.9, bold=True)
text("JP1 TERM", 82.6, 59.25)

# test points, status LEDs
text("12V", 12.6, 43.6); text("GND", 15.6, 43.6)
text("5V", 46.5, 38.6); text("3V3", 40.5, 27.2)
text("STATUS", 57.6, 43.3, bold=True, rot=90)
for i, s in enumerate(["R", "G", "Y", "L"]):
    text(s, 60.0 + 3.8 * i + 1.9, 45.25)

pcbnew.SaveBoard(sys.argv[2], b)
print("relabelled")
