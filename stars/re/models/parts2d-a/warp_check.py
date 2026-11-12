"""Checks the curve model of 0x9989 against the curve arrays dumped at 0x9a62 (runs/warp2), and the patch fill
0x9a62 driven by the dumped curves against the capture."""
import sys

import numpy as np

import warp_model as W
from common import *

pre = sys.argv[1] if len(sys.argv) > 1 else 'runs/warp2'
frames = Frames(pre + '.frames')
f0 = int([l for l in open(pre + '.ticks') if not l.startswith('#')][0].split()[0])
tex = W.textures()
L = [l.split() for l in open(pre + '.hook')]
ncur = ncur_ok = nimg = nimg_ok = 0
badc, badi = [], []
for i in range(len(L) - 2):
    if L[i][0] != '9989' or L[i + 1][0] != '9a62' or L[i + 2][0] != '9d14':
        continue
    p0 = W.parse(L[i])            # parameters when the curves were computed
    p1 = W.parse(L[i + 1])
    m = L[i + 1][11:]
    c = np.frombuffer(bytes.fromhex(m[3]), '<i4').reshape(-1, 2)
    top, bot, left, right = c[0:320], c[320:640], c[640:840], c[840:1040]   # 0x6990, 0x7390, 0x7d90, 0x83d0
    mt, mb, mr, ml = W.curves(p0)
    ncur += 1
    same = (np.array_equal(np.array(mt), top) and np.array_equal(np.array(mb), bot) and
            np.array_equal(np.array(mr), right) and np.array_equal(np.array(ml), left))
    ncur_ok += same
    if not same:
        badc.append(int(L[i][1]))
    tl = lambda a: [tuple(int(v) for v in r) for r in a]
    img = W.draw(tl(top), tl(bot), tl(right), tl(left), p1['u0'], p1['v0'], tex[p1['pic']])
    fe = int(L[i + 2][1])
    d = int(np.count_nonzero(frames.vga(fe - f0) != img))
    nimg += 1
    nimg_ok += d == 0
    if d:
        badi.append((fe, d))
print('curves: %d frames, %d equal to the model; differing at frames %s' % (ncur, ncur_ok, badc[:20]))
print('pictures from the dumped curves: %d frames, %d exact; differing %s' % (nimg, nimg_ok, badi[:20]))
