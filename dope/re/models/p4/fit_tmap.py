"""Fit the swirl map at 07d9 (32000 words, fire pixel p = y*160 + x uses word 31999 - p; word = v*256 + u) with a
polar formula: u = angle, v = scaled radius. Prints the best parameters and how many entries match exactly."""
import itertools
import os

import numpy as np

RE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
img = open(os.path.join(RE, 'unpacked', 'p4.bin'), 'rb').read()
t = np.frombuffer(img[0x7d90:0x7d90 + 64000], '<u2')
m = t[::-1].reshape(200, 160).astype(np.int64)
U = m & 255
V = m >> 8
y, x = np.mgrid[0:200, 0:160].astype(np.float64)


def model(cx, cy, ax, ay, rnd):
    dx = (x - cx) * ax
    dy = (y - cy) * ay
    v = np.sqrt(dx * dx + dy * dy)
    th = np.arctan2(dy, -dx) * 128 / np.pi
    return rnd(v), rnd(th) & 255


# least squares on v for the scale (centre guessed from the minimum)
best = None
for cx in np.arange(79.0, 81.01, 0.5):
    for cy in np.arange(99.0, 101.01, 0.5):
        r2x = (x - cx) ** 2
        r2y = (y - cy) ** 2
        # v^2 = ax^2 r2x + ay^2 r2y
        A = np.stack([r2x.ravel(), r2y.ravel()], 1)
        sol, res, *_ = np.linalg.lstsq(A, (V.astype(float) ** 2).ravel(), rcond=None)
        ax, ay = np.sqrt(sol)
        for name, rnd in (('floor', lambda a: np.floor(a).astype(np.int64)), ('round', lambda a: np.floor(a + 0.5).astype(np.int64))):
            v, u = model(cx, cy, ax, ay, rnd)
            ok = (v == V).mean(), (u == U).mean()
            if best is None or sum(ok) > sum(best[0]):
                best = (ok, cx, cy, ax, ay, name)
print('best', best)
ok, cx, cy, ax, ay, name = best
print('ax/ay', ax / ay, '1/ax', 1 / ax, '1/ay', 1 / ay)


def final_fit():
    """the polar form used in notes-p4.md: centre = screen centre, anisotropy 0.7, floor for v, round for u"""
    cx, cy, k = 79.5, 99.5, 0.7
    r = np.sqrt((x - cx) ** 2 + (k * (y - cy)) ** 2)
    A = np.stack([np.ones(r.size), r.ravel()], 1)
    (a, b), *_ = np.linalg.lstsq(A, V.ravel().astype(float) + 0.5, rcond=None)
    v = np.minimum(np.floor(a + b * r), 255).astype(np.int64)
    th = np.arctan2(k * (y - cy), -(x - cx)) * 128 / np.pi
    u = np.floor(th + 0.5).astype(np.int64) & 255
    e = ((u - U + 128) & 255) - 128
    print('v = floor(%.4f + %.4f * sqrt((x-79.5)^2 + (0.7 (y-99.5))^2)) [fire pixels]: exact %.3f, within 1 %.4f, '
          'max error %d, entries 255: %d' % (a, b, (v == V).mean(), (abs(v - V) <= 1).mean(), abs(v - V).max(),
                                            (V == 255).sum()))
    print('u = round(128/pi atan2(0.7 (y-99.5), -(x-79.5))) & 255: exact %.3f, within 1 %.4f, within 2 %.4f, max %d'
          % ((e == 0).mean(), (abs(e) <= 1).mean(), (abs(e) <= 2).mean(), abs(e).max()))
    big = np.argwhere(abs(e) > 2)
    print('entries off by more than 2 in u (near the axes, a generator quirk):', len(big),
          'rows', sorted(set(big[:, 0].tolist()))[:10], 'cols', sorted(set(big[:, 1].tolist()))[:10])


final_fit()
