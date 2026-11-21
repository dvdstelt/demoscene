"""Finer checks of the shade tables: inmask.shd vs logo.shd rows, and candidate generators."""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
def pal(n): return np.frombuffer(d.get(n), np.uint8).reshape(256, 3).astype(float)
def shd(n, k): return np.frombuffer(d.get(n), np.uint8).reshape(k, 256)
LP, LS, MS = pal('logo/logo.pal'), shd('logo/logo.shd', 64), shd('logo/inmask.shd', 16)
IP, IS = pal('intro/intro.pal'), shd('intro/intro.shd', 64)
for m in range(16):
    best = max(range(64), key=lambda L: (MS[m] == LS[L]).mean())
    print('inmask row', m, '== logo.shd row', best, 'fraction equal', (MS[m] == LS[best]).mean())

def nearest(P, target):
    # nearest palette entry (squared RGB distance), first index on ties
    dd = ((target[:, None, :] - P[None, :, :]) ** 2).sum(-1)
    return dd.argmin(1)

for name, P, S in (('intro', IP, IS), ('logo', LP, LS)):
    print('==', name)
    for desc, f in (('L/63', lambda L: L / 63), ('L/64', lambda L: L / 64), ('(L+1)/64', lambda L: (L + 1) / 64),
                    ('L/32', lambda L: L / 32), ('L/31', lambda L: L / 31)):
        rows = range(0, 33) if name == 'logo' else range(64)
        tot = 0
        for L in rows:
            for r in (lambda x: x, np.floor, np.round):
                pass
            t = nearest(P, P * f(L))
            tot += (t == S[L]).mean()
        tf = nearest(P, np.floor(P * f(31 if name == 'logo' else 40)))
        print(f'   scale {desc}: mean exact match over rows {tot / len(rows):.3f}')
    # per-row best factor for a few rows (colour error)
    for L in (8, 16, 24, 40, 48, 56, 63):
        got = P[S[L]]
        if name == 'logo' and L > 32:
            # additive toward white: got = P + k*(63-P)? or got = min(P*s,63)? or P + add clamp
            adds = [np.abs(got - np.clip(P + a, 0, 63)).mean() for a in range(0, 64)]
            muls = [np.abs(got - np.clip(P * s, 0, 63)).mean() for s in np.arange(1, 3, 0.01)]
            whites = [np.abs(got - (P + k * (63 - P))).mean() for k in np.arange(0, 1, 0.01)]
            print(f'   L{L}: best add {int(np.argmin(adds))} err {min(adds):.2f}; best mul {1 + 0.01 * np.argmin(muls):.2f} err {min(muls):.2f}; best white {0.01 * np.argmin(whites):.2f} err {min(whites):.2f}')
