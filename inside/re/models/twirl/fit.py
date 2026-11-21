"""Compare the pre-table with the continuous polar coordinates of each pixel centre.

row = (j >> 2) = v + 128, col = (i >> 2) = u + 128.  Continuous: rho = |p - (160,100)|, theta = atan2(dy, dx) in
[0, 2pi) with p = pixel centre (x + 0.5, y + 0.5).  Prints the mean and spread of (row + 0.5) - rho / 1.72 and of
(col + 0.5) - theta * 256 / 2pi (wrapped), i.e. how far the table's texel centre is from the continuous coordinate.
"""
import math, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
_p = np.load(os.path.join(HERE, 'pre.npz')); u, v = _p['u'], _p['v']
y, x = np.mgrid[0:200, 0:320]
dx = x + 0.5 - 160; dy = y + 0.5 - 100
rho = np.hypot(dx, dy); th = np.mod(np.arctan2(dy, dx), 2 * math.pi)
row = v + 128; col = u + 128
er = (row + 0.5) - rho / 1.72
ec = (col + 0.5) - th * 256 / (2 * math.pi)
ec = (ec + 128) % 256 - 128
for name, e, sel in [('row', er, rho > 3), ('col', ec, rho > 3), ('col rho>40', ec, rho > 40)]:
    e = e[sel]
    print(f'{name:10s} mean {e.mean():+.3f}  std {e.std():.3f}  min {e.min():+.3f}  max {e.max():+.3f}')
# the far corner of the pixel (largest radius) for the row
far = np.hypot(np.where(dx > 0, dx + 0.5, dx - 0.5), np.where(dy > 0, dy + 0.5, dy - 0.5))
e2 = row - np.floor(np.floor(far / 0.43 + 1e-9) / 4)
print('row == floor(floor(rho_far/0.43)/4):', float((e2 == 0).mean()))
