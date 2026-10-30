# reveal_latency.py <burst dir> <command text> — theme reveal: tap → first wipe frame, and the
# covered-area metric (1 % → 98.5 %) used against the reference (0.50 s). The caption bar is
# excluded (it is outside the snapshot and switches at once).
import sys, os, csv
import numpy as np
from PIL import Image
d, key = sys.argv[1], sys.argv[2]
rows = list(csv.DictReader(open(os.path.join(d, 'timestamps.csv'))))
fr = {int(r['index']): int(r['unix_ms']) for r in rows if r['kind'] == 'frame'}
cmd = [r for r in rows if r['kind'] == 'command' and r['detail'] == key][0]
applog = [l.split(' ', 1) for l in open(os.path.join(d, 'app-commands.log')) if l[0].isdigit()]
t_tap = next(int(t) for t, c in applog if c.strip() == key and int(t) >= int(cmd['unix_ms']) - 5)
idx = sorted(fr)
L = lambda i: np.asarray(Image.open(os.path.join(d, f'f{i:03d}.png')).convert('L')).astype(float)[30:, :]
st = L(int(cmd['index']) - 1); fin = L(idx[-1]); diff = np.abs(st - fin) > 40
area = {i: ((np.abs(L(i) - fin) < np.abs(L(i) - st)) & diff).sum() / max(1, diff.sum()) for i in idx}
first = next(i for i in idx if fr[i] > t_tap and area[i] > 0.0005)
a1 = next(i for i in idx if area[i] > 0.01); a985 = next(i for i in idx if area[i] >= 0.985)
prev = max(i for i in idx if fr[i] <= t_tap)
print(f'{os.path.basename(d)}: tap→first wipe frame {fr[first]-t_tap} ms (capture spacing {fr[first]-fr[prev]} ms); '
      f'area 1 %→98.5 % {fr[a985]-fr[a1]} ms')
