"""DTABLE (part II's 1x1 two-plane effect): compare dtable.raw / dtable2.raw (256 x 128 indices, own palette) with a
50 % blend of the two textures' palette colours: DTABLE[b][a] ~ (pal1[a] + pal2[b]) / 2, a = plane 1 texel
(plo3 / plo2), b = plane 2 texel (plo6 / plo5). Prints the rms error for blend weights and the nearest-colour test.

    python3 models/bitmap/dtable.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bitmap import read_raw  # noqa: E402

for dt, p1, p2 in [('pics/dtable.raw', 'pics/plo3.raw', 'pics/plo6.raw'),
                   ('pics/dtable2.raw', 'pics/plo2.raw', 'pics/plo5.raw')]:
    pal, px = read_raw(dt, 2)
    pal = pal.astype(float)
    t = px.reshape(128, 256)[:, :128]                 # texel values are < 128 in both planes
    rgb = pal[t]
    c1 = read_raw(p1, 2)[0].astype(float)[:128][None]
    c2 = read_raw(p2, 2)[0].astype(float)[:128][:, None]
    for w in (0.3, 0.4, 0.5, 0.6, 0.7):
        e = rgb - (w * c1 + (1 - w) * c2)
        print('%s: weight of plane 1 %.1f: rms error %.2f (8-bit units)' % (dt, w, np.sqrt((e ** 2).mean())))
    m = (c1 + c2) / 2
    d = ((m[:, :, None, :] - pal[None, None, :, :]) ** 2).sum(-1)
    print('   nearest palette colour of the 50 %% blend == DTABLE entry: %.1f %%' % (100 * (d.argmin(-1) == t).mean()))
    e = rgb - m
    print('   |error| 99th percentile %.0f, max %s' % (np.percentile(np.abs(e), 99), np.abs(e).max(axis=(0, 1))))
    # dithered? the error of 4 x 4 block averages (in (a, b)) is much smaller than per entry if it is
    for k in (2, 4, 8):
        eb = e.reshape(128 // k, k, 128 // k, k, 3).mean(axis=(1, 3))
        print('   rms error of %dx%d block means %.2f' % (k, k, np.sqrt((eb ** 2).mean())))
