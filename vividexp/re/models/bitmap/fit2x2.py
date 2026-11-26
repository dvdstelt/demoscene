"""Fits of the three 2x2 tables (160x100 entries, one per 2x2 block) to continuous formulas; prints the parameters
and how many bytes the formula reproduces (after truncation like the generator) and the error distribution."""
import numpy as np, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gn import lm
from tabfit import tab, score
y, x = np.mgrid[0:100, 0:160].astype(float)


def unwrap2(t):
    t = t.astype(float)
    col = np.unwrap(t[:, 0] * 2 * np.pi / 256) * 256 / (2 * np.pi)
    out = np.zeros_like(t)
    for i in range(t.shape[0]):
        row = np.unwrap(t[i] * 2 * np.pi / 256) * 256 / (2 * np.pi)
        out[i] = row - row[0] + col[i]
    return out


for n in ['tab4.2x2', 'tab5.2x2']:
    t = tab(n); u = unwrap2(t)
    A = np.stack([x.ravel(), y.ravel(), np.ones(x.size), -(x * u).ravel(), -(y * u).ravel()], 1)
    sol = np.linalg.lstsq(A, u.ravel(), rcond=None)[0]
    f = lambda p: ((p[0] * x + p[1] * y + p[2]) / (p[3] * x + p[4] * y + 1) - u - 0.5).ravel()   # trunc ~ value - 0.5
    p = lm(f, sol)
    v = (p[0] * x + p[1] * y + p[2]) / (p[3] * x + p[4] * y + 1)
    for m in ['trunc', 'floor', 'round']:
        nok, mx, e = score(v, t, m)
        print(n, m, 'exact %d/16000' % nok, 'max err', mx, 'hist', np.unique(e, return_counts=True))
    print('   u = (%.6g*x + %.6g*y + %.6g) / (%.6g*x + %.6g*y + 1)   (x 0..159, y 0..99 block coordinates)' % tuple(p))
t = tab('tab6.2x2')
dx = x - 80; dy = y - 50
r = np.sqrt(dx * dx + dy * dy); th = np.arctan2(dy, dx)
for c in [np.pi / 64, 0.04924]:
    v = th * 128 / np.pi + 16 * np.cos(r * c)
    nok, mx, e = score(v, t, 'trunc')
    print('tab6.2x2 c=%.6f exact %d/16000 max err %d' % (c, nok, mx), np.unique(e, return_counts=True))
