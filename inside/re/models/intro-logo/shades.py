"""How the shade tables of the intro and the logo relate to their palettes (fit of pal[shd[L][c]] against pal[c])."""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
def pal(n): return np.frombuffer(d.get(n), np.uint8).reshape(256, 3).astype(float)
def shd(n, k): return np.frombuffer(d.get(n), np.uint8).reshape(k, 256)

for sname, pname, k in (('intro/intro.shd', 'intro/intro.pal', 64), ('logo/logo.shd', 'logo/logo.pal', 64),
                        ('logo/inmask.shd', 'logo/logo.pal', 16)):
    P, S = pal(pname), shd(sname, k)
    print('==', sname, 'identity levels:', [L for L in range(k) if (S[L] == np.arange(256)).mean() > 0.5],
          'identity fraction per level (every 8th):', [round((S[L] == np.arange(256)).mean(), 2) for L in range(0, k, max(1, k // 8))])
    print('   max palette value', P.max())
    for L in list(range(0, k, max(1, k // 16))) + [k - 1]:
        got = P[S[L]]
        # best scalar a in got ~ a * P (least squares over all colours)
        a = (got * P).sum() / max((P * P).sum(), 1)
        # model 'towards white' : got ~ P + b*(63-P)
        b = ((got - P) * (63 - P)).sum() / max(((63 - P) ** 2).sum(), 1)
        err_a = np.abs(got - np.clip(a * P, 0, 63)).mean()
        err_b = np.abs(got - (P + b * (63 - P))).mean()
        print(f'   L{L:2d}: scale fit a={a:.3f} (mean err {err_a:.2f}); towards-white fit b={b:.3f} (err {err_b:.2f}); mean {got.mean():.1f} vs {P.mean():.1f}')
