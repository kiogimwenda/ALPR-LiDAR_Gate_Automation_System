"""Rev 2.2: J1 power input becomes a Phoenix MC 1,5/2-G-3.81 pluggable header
(same family as J7/J8). python.exe swap_j1.py in.kicad_pcb out.kicad_pcb"""
import sys

import pcbnew

from geo import B, mm

FPDIR = r"C:\Program Files\KiCad\10.0\share\kicad\footprints"
LIB, NAME = "Connector_Phoenix_MC", "PhoenixContact_MC_1,5_2-G-3.81_1x02_P3.81mm_Horizontal"
b = pcbnew.LoadBoard(sys.argv[1])
old = [f for f in b.GetFootprints() if f.GetReference() == "J1"][0]
keep = dict(path=old.GetPath(), sheetname=old.GetSheetname(), sheetfile=old.GetSheetfile(),
            value=old.GetValue(), fields={n: old.GetField(n).GetText() for n in ("Datasheet", "Description") if old.GetField(n)},
            nets={p.GetNumber(): p.GetNet() for p in old.Pads()})
b.Delete(old)

fp = pcbnew.FootprintLoad(FPDIR + "\\" + LIB + ".pretty", NAME)
fp.SetFPID(pcbnew.LIB_ID(LIB, NAME))
fp.SetReference("J1"); fp.SetValue(keep["value"])
fp.SetPath(keep["path"]); fp.SetSheetname(keep["sheetname"]); fp.SetSheetfile(keep["sheetfile"])
for n, v in keep["fields"].items():
    f = fp.GetField(n)
    if f is None:
        fp.AddField(pcbnew.PCB_FIELD(fp, -1, n)); f = fp.GetField(n)
    f.SetText(v); f.SetVisible(False)
b.Add(fp)
# plug mates from footprint +y; rotation 270 turns that to -x = out of the left board edge,
# body flush with the edge (courtyard local y max 8.5). Pin 1 (+12V) on top.
x, y = B(8.5, 35.5)
fp.SetOrientationDegrees(0); fp.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y))); fp.SetOrientationDegrees(270)
for p in fp.Pads():
    p.SetNet(keep["nets"][p.GetNumber()])
fp.Reference().SetLayer(pcbnew.F_Fab)
pins = {p.GetNumber(): p.GetPosition() for p in fp.Pads()}

# silkscreen: polarity marks follow the new pin positions; back-side revision
for d in b.GetDrawings():
    if not isinstance(d, pcbnew.PCB_TEXT):
        continue
    if d.GetLayer() == pcbnew.F_SilkS and d.GetText() in ("+", "-") and pcbnew.ToMM(d.GetPosition().x) < 115:
        tgt = pins["1"] if d.GetText() == "+" else pins["2"]
        d.SetPosition(pcbnew.VECTOR2I(mm(B(12.15, 0)[0]), tgt.y))   # clear of the header body silk
    if d.GetLayer() == pcbnew.B_SilkS and "rev 2.1" in d.GetText():
        d.SetText(d.GetText().replace("rev 2.1", "rev 2.2"))
pcbnew.SaveBoard(sys.argv[2], b)
print("J1 pins:", {k: (round(pcbnew.ToMM(v.x) - 100, 2), round(pcbnew.ToMM(v.y) - 50, 2)) for k, v in pins.items()})
