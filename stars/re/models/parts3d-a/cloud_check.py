"""Cloud frames: cloud_model.draw with the callback's speed sum S(n) against full.frames (rows 1..197).

n = PIT ticks since the callback install: the frame record's PIT count - 5386 (the draw happens in the frame before
the record, after the retrace). For each frame the closest n (within +-1) that reproduces it is reported.
"""
import numpy as np
from common import out, frame, ticks
from cloud_model import draw


def S(n):
    """Speed sum of callback 0x1278e after n ticks (1/1024 texel)."""
    s = 0
    for k in range(1, n + 1):
        s += 0x400 if k <= 0x124 else 0x400 - (0x400 * (k - 0x124)) // 0xe4
    return s


T = np.load(out('cloud_table.npy'))
tk = ticks()
for f in [5425, 5430, 5440, 5480, 5600, 5700, 5713, 5714, 5715, 5716, 5720, 5750, 5800, 5900, 5940, 5941]:
    pix, pal = frame(f)
    n0 = int(tk[f][2]) - 5386
    res = []
    for n in (n0, n0 - 1, n0 + 1):
        d = int((draw(T, S(n))[1:198] != pix[1:198]).sum())
        res.append((d, n))
        if d == 0:
            break
    d, n = min(res)
    print('frame %d: n = %d (record n0 = %d), S/1024 = %.3f, differing pixels %d' % (f, n, n0, S(n) / 1024, d), flush=True)
