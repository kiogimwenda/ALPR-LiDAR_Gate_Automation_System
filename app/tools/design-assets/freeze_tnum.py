# Freeze the OpenType 'tnum' feature into the cmap for the DIGITS ONLY
# (U+0030–U+0039 -> their tabular glyphs) and rename the family so the
# modified font is distinguishable (OFL 1.1 FAQ). Punctuation (hyphen,
# colon, comma, period) keeps its proportional glyph: freezing all of tnum
# turned "gate-demo-01" into "gate - demo - 01".
import sys
from fontTools.ttLib import TTFont
src, dst, family, style = sys.argv[1:5]
f = TTFont(src)
gsub = f['GSUB'].table
subst = {}
for fr in gsub.FeatureList.FeatureRecord:
    if fr.FeatureTag != 'tnum':
        continue
    for li in fr.Feature.LookupListIndex:
        lk = gsub.LookupList.Lookup[li]
        for st in lk.SubTable:
            if lk.LookupType == 7:
                st = st.ExtSubTable
            if hasattr(st, 'mapping'):
                subst.update(st.mapping)
changed = 0
for t in f['cmap'].tables:
    for cp, g in list(t.cmap.items()):
        if 0x30 <= cp <= 0x39 and g in subst:
            t.cmap[cp] = subst[g]; changed += 1
name = f['name']
full = f'{family} {style}' if style != 'Regular' else family
ps = f'{family.replace(" ", "")}-{style}'
for rec in name.names:
    if rec.nameID in (1, 16):
        rec.string = family
    elif rec.nameID in (2, 17):
        rec.string = style if rec.nameID == 17 else rec.toUnicode()
    elif rec.nameID == 4:
        rec.string = full
    elif rec.nameID == 6:
        rec.string = ps
    elif rec.nameID == 3:
        rec.string = f'{ps};frozen-tnum'
# nameID 1/2 must be RIBBI-legal: family+weight for non-RIBBI styles
if style not in ('Regular', 'Bold'):
    for rec in name.names:
        if rec.nameID == 1:
            rec.string = f'{family} {style}'
        if rec.nameID == 2:
            rec.string = 'Regular'
f.save(dst)
print(dst, 'digits remapped:', changed, 'tnum pairs:', len(subst))
