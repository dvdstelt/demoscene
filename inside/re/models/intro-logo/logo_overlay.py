"""Check the logo's 2D overlays: from the draw target after the 3D scene (t_3d) to the finished frame (t_final):
the shadow mask inmask.raw (253 x 147, levels 1..15, 0 = transparent) at (31, 9) through inmask.shd, then from 4:32
on the text inside.raw (249 x 139, index 0 transparent) at (34, 13); and the shown frame against full.frames."""
import os, sys, pickle
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
from sheet import open_frames, indexed
D = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
MASK = np.frombuffer(D.get('logo/inmask.raw'), np.uint8).reshape(147, 253)
MSHD = np.frombuffer(D.get('logo/inmask.shd'), np.uint8).reshape(16, 256)
TEXT = np.frombuffer(D.get('logo/inside.raw'), np.uint8).reshape(139, 249)


def overlay(img, text_on):
    out = img.copy()
    sub = out[9:9 + 147, 31:31 + 253]
    m = MASK != 0
    sub[m] = MSHD[MASK[m], sub[m]]
    if text_on:
        sub = out[13:13 + 139, 34:34 + 249]
        t = TEXT != 0
        sub[t] = TEXT[t]
    return out


ev = pickle.load(open(sys.argv[1], 'rb'))
t3d = {e['n']: e for e in ev if e['kind'] == 't_3d'}
fin = {e['n']: e for e in ev if e['kind'] == 't_final'}
raw, _ = open_frames(os.path.join(HERE, '..', '..', 'full.frames'))
flips = [l.split() for l in open(os.path.join(HERE, '..', '..', 'full.flips')) if not l.startswith('#')]
ok = tot = 0
for n in sorted(fin):
    if n not in t3d:
        continue
    a = np.frombuffer(t3d[n]['target'], np.uint8).reshape(200, 320)
    b = np.frombuffer(fin[n]['target'], np.uint8).reshape(200, 320)
    pred = overlay(a, fin[n]['text'] != 0)
    tot += 1; ok += (pred == b).all()
print('overlay exact in %d of %d frames' % (ok, tot))
# the finished frame is what the screen shows from the next refresh on (full.frames record)
shown = 0; checked = 0
for n in sorted(fin):
    r = fin[n]['refresh']
    b = np.frombuffer(fin[n]['target'], np.uint8).reshape(200, 320)
    for k in (r, r + 1, r + 2):
        px, _ = indexed(raw, k)
        if (px == b).all():
            shown += 1
            break
    checked += 1
print('finished frames found in full.frames at refresh r..r+2: %d of %d' % (shown, checked))
