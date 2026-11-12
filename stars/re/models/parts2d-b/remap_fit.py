"""Test what the 32 remap tables of file 35 are: table k maps colour c to the palette colour nearest to
pal[c] * f(k)? Prints the best scale per table and how many entries a nearest-colour rule reproduces."""
import numpy as np
from face_data import file35
f, pal, tables = file35()
P = pal.astype(np.float64)
np.set_printoptions(linewidth=200)
print(pal.tolist())
def nearest(target, metric='sq'):
    d = ((target[:, None, :] - P[None, :, :]) ** 2).sum(-1)
    return d.argmin(1), d
for k in range(32):
    tb = tables[k].astype(int)
    best = None
    for s in np.arange(0.0, 2.6, 0.005):
        idx, d = nearest(P * s)
        # count entries where the table's colour is at the minimum distance (ties allowed)
        ok = (d[np.arange(256), tb] <= d.min(1) + 1e-9).sum()
        if best is None or ok > best[0]: best = (ok, s)
    # also additive?
    print(k, 'best scale %.3f' % best[1], 'matches', best[0])
