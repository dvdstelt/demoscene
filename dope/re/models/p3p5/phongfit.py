"""Fit a formula to p3's Phong map (left half of the RIX picture at 0306:0006): index = f(distance from (55, 40))."""
import os
import numpy as np
RE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
img = open(os.path.join(RE, 'unpacked', 'p3.bin'), 'rb').read()
q = np.frombuffer(img[0x3370:0x3370 + 32768], np.uint8).reshape(128, 256)[:, :128].astype(int)
y, x = np.mgrid[0:128, 0:128]
best = None
for cx in np.arange(54.5, 55.6, 0.1):
    for cy in np.arange(39.5, 40.6, 0.1):
        d = np.hypot(x - cx, y - cy)
        for off in np.arange(-0.6, 0.61, 0.05):
            v = np.clip(np.floor(240.5 - d + off), 128, 239)
            v = np.where(d < 4.4, 239, v)
            e = np.abs(v - q)
            score = (e.max(), (e > 0).sum())
            if best is None or score < best[0]:
                best = (score, cx, cy, off)
(emax, n), cx, cy, off = best
print('best: centre (%.1f, %.1f), index = clamp(floor(240.5 - d %+.2f), 128, 239), 239 inside d < 4.4: max error %d, %d of 16384 texels differ' % (cx, cy, off, emax, n))
d = np.hypot(x - cx, y - cy)
used = np.hypot(x - 64, y - 64) <= 54 + 8
v = np.clip(np.floor(240.5 - d + off), 128, 239); v = np.where(d < 4.4, 239, v)
print('inside the used disc (r <= 62 around (64,64)): max error', np.abs(v - q)[used].max(), 'differing', ((v != q) & used).sum(), 'of', used.sum())
print('min index in used disc', q[used].min(), 'max', q[used].max())
