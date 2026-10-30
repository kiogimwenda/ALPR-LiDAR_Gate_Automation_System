# pill.py <burst dir> [dark|light] — nav pill left/right edge per frame (px in the saved frames) with ms since the first command
import sys, csv, os
import numpy as np
from PIL import Image
d = sys.argv[1]; theme = sys.argv[2] if len(sys.argv) > 2 else 'light'
rows = list(csv.DictReader(open(os.path.join(d, 'timestamps.csv'))))
fr = {int(r['index']): int(r['unix_ms']) for r in rows if r['kind'] == 'frame'}
cmds = [(int(r['index']), int(r['unix_ms']), r['detail']) for r in rows if r['kind'] == 'command']
t0 = cmds[0][1]
out = []
for i in sorted(fr):
    a = np.asarray(Image.open(os.path.join(d, f'f{i:03d}.png')).convert('L')).astype(int)
    band = a[35:80, 150:520]   # header row, nav region
    mask = band < 60 if theme == 'light' else band > 200
    cols = np.where(mask.sum(0) >= 10)[0]   # pill columns: tall dark runs (text is thin)
    if len(cols) == 0:
        out.append((i, fr[i] - t0, None, None)); continue
    out.append((i, fr[i] - t0, int(cols.min()) + 150, int(cols.max()) + 150))
for o in out: print(*o)
