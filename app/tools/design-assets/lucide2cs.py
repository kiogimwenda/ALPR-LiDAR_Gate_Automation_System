# Lucide SVGs -> C# path-data strings (MAUI geometry mini-language), all
# shapes folded into one path; numbers re-tokenised with explicit spaces.
import re, sys, glob, os
import xml.etree.ElementTree as ET
NUM = re.compile(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?')
ARGS = dict(M=2, L=2, H=1, V=1, C=6, S=4, Q=4, T=2, A=7, Z=0)
def norm_path(d):
    out = []; i = 0; cmd = None; first = True; pending = None
    while i < len(d):
        ch = d[i]
        if ch.isspace() or ch == ',': i += 1; continue
        if ch.isalpha():
            cmd = ch; pending = ch; i += 1
            if cmd in 'zZ': out.append('Z'); pending = None
            continue
        n = ARGS[cmd.upper()]
        vals = []
        for k in range(n):
            while i < len(d) and (d[i].isspace() or d[i] == ','): i += 1
            if cmd.upper() == 'A' and k in (3, 4):
                vals.append(d[i]); i += 1; continue
            m = NUM.match(d, i); vals.append(m.group()); i = m.end()
        letter = pending if pending else cmd
        if letter == 'm' and first: letter = 'M'   # an element's first moveto is absolute
        out.append(letter + ' ' + ' '.join(vals)); first = False
        if cmd == 'm': cmd = 'l'
        elif cmd == 'M': cmd = 'L'
        pending = None
    return ' '.join(out)
def f(x): return ('%.3f' % float(x)).rstrip('0').rstrip('.')
def shape(el):
    tag = el.tag.split('}')[-1]; a = el.attrib
    if tag == 'path': return norm_path(a['d'])
    if tag == 'circle':
        cx, cy, r = float(a['cx']), float(a['cy']), float(a['r'])
        return f'M {f(cx-r)} {f(cy)} A {f(r)} {f(r)} 0 1 0 {f(cx+r)} {f(cy)} A {f(r)} {f(r)} 0 1 0 {f(cx-r)} {f(cy)} Z'
    if tag == 'ellipse':
        cx, cy, rx, ry = float(a['cx']), float(a['cy']), float(a['rx']), float(a['ry'])
        return f'M {f(cx-rx)} {f(cy)} A {f(rx)} {f(ry)} 0 1 0 {f(cx+rx)} {f(cy)} A {f(rx)} {f(ry)} 0 1 0 {f(cx-rx)} {f(cy)} Z'
    if tag == 'line':
        return f"M {a['x1']} {a['y1']} L {a['x2']} {a['y2']}"
    if tag in ('polyline', 'polygon'):
        p = NUM.findall(a['points']); pts = [f'{p[i]} {p[i+1]}' for i in range(0, len(p), 2)]
        return 'M ' + ' L '.join(pts) + (' Z' if tag == 'polygon' else '')
    if tag == 'rect':
        x, y, w, h = (float(a.get(k, 0)) for k in ('x', 'y', 'width', 'height'))
        rx = float(a.get('rx', a.get('ry', 0)))
        if rx == 0: return f'M {f(x)} {f(y)} H {f(x+w)} V {f(y+h)} H {f(x)} Z'
        return (f'M {f(x+rx)} {f(y)} H {f(x+w-rx)} A {f(rx)} {f(rx)} 0 0 1 {f(x+w)} {f(y+rx)} V {f(y+h-rx)} '
                f'A {f(rx)} {f(rx)} 0 0 1 {f(x+w-rx)} {f(y+h)} H {f(x+rx)} A {f(rx)} {f(rx)} 0 0 1 {f(x)} {f(y+h-rx)} '
                f'V {f(y+rx)} A {f(rx)} {f(rx)} 0 0 1 {f(x+rx)} {f(y)} Z')
    raise SystemExit('unsupported ' + tag)
src, dst = sys.argv[1:3]
lines = ['// Icons.cs — GENERATED from Resources/Icons/*.svg (Lucide, ISC licence:',
         '// Resources/Icons/LICENSE.txt) by app/tools/design-assets/lucide2cs.py; do not edit.',
         '// Each icon is one stroked path on a 24×24 grid (stroke 2, round caps).', '',
         'namespace GateControl.Controls;', '', 'public static class Icons', '{']
for p in sorted(glob.glob(os.path.join(src, '*.svg'))):
    name = ''.join(w.capitalize() for w in os.path.basename(p)[:-4].split('-'))
    root = ET.parse(p).getroot()
    data = ' '.join(shape(el) for el in root)
    lines.append(f'    public const string {name} = "{data}";')
lines.append('}')
open(dst, 'w').write('\n'.join(lines) + '\n')
print('wrote', dst)
