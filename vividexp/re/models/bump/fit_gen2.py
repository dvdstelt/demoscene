"""Residuals of the light spot and the ring profiles against their closest formulas. usage: python3 fit_gen2.py"""
import numpy as np

import bump

px = bump.pcx('bump/phong.pcx')[0].astype(float)
yy, xx = np.mgrid[0:256, 0:256]
for cx, cy in ((128, 128), (127.5, 127.5), (128.5, 128.5)):
    r = np.hypot(xx - cx, yy - cy)
    m = (r < 120) & (r > 2)
    res = px[m] - (127 - r[m])
    print('centre', cx, cy, 'residual range %.3f .. %.3f, mean %.3f, std %.3f' % (res.min(), res.max(), res.mean(), res.std()))
    h, e = np.histogram(res, bins=12)
    print('  hist', h.tolist(), np.round(e, 2).tolist())
# integer distance? r2 = dx^2 + dy^2; px as a function of r2
r2 = ((xx - 128) ** 2 + (yy - 128) ** 2)
amb = 0
for v in np.unique(r2[r2 < 120 ** 2]):
    s = px[r2 == v]
    if s.max() != s.min():
        amb += 1
print('integer r^2 values with more than one pixel value:', amb)
print('px row 128, x=128..150', px[128, 128:150].astype(int).tolist())
print('127 - r          ', np.round(127 - np.hypot(xx[128, 128:150] - 128, 0), 2).tolist())
print('px diagonal       ', [int(px[128 + k, 128 + k]) for k in range(0, 40, 2)])
print('127 - k*sqrt2     ', [round(127 - k * 2 ** .5, 2) for k in range(0, 40, 2)])
# symmetry
print('symmetric x', np.array_equal(px[:, 1:], px[:, 1:][:, ::-1]), 'symmetric y', np.array_equal(px[1:], px[1:][::-1]),
      'transpose', np.array_equal(px, px.T))
d = px[1:, 1:] - px[1:, 1:][:, ::-1]
print('x-mirror about 128 differences', np.unique(d, return_counts=True))
