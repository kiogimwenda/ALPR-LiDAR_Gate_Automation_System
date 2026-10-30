# sheet.py <burst dir> <out.jpg> <first> <last> [step] [cols] — labelled contact sheet with ms since the command
import sys, csv, os
from PIL import Image, ImageDraw
d, out, a, b = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
step = int(sys.argv[5]) if len(sys.argv) > 5 else 1
cols = int(sys.argv[6]) if len(sys.argv) > 6 else 4
rows = list(csv.DictReader(open(os.path.join(d, 'timestamps.csv'))))
fr = {int(r['index']): int(r['unix_ms']) for r in rows if r['kind'] == 'frame'}
cmds = [int(r['unix_ms']) for r in rows if r['kind'] == 'command']
t0 = cmds[0] if cmds else fr[0]
idx = list(range(a, b + 1, step))
ims = [Image.open(os.path.join(d, f'f{i:03d}.png')) for i in idx]
w, h = ims[0].size
W = Image.new('RGB', (cols * w, ((len(ims) + cols - 1) // cols) * (h + 16)), 'white')
dr = ImageDraw.Draw(W)
for k, (i, im) in enumerate(zip(idx, ims)):
    x, y = (k % cols) * w, (k // cols) * (h + 16)
    W.paste(im, (x, y + 16)); dr.text((x + 4, y + 2), f'f{i}  t={fr[i]-t0:+d} ms', fill='red')
W.save(out, quality=85)
