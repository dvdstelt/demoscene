"""bumptab.val against 128 + A*d/sqrt(d^2 + D^2) (a point light at height D over the screen centre), with several
roundings and centres. usage: python3 fit_tab.py"""
import numpy as np

import bump

tab = np.frombuffer(bump.load('bump/bumptab.val'), np.uint8).astype(int)
R = {'floor': np.floor, 'trunc': np.trunc, 'round': lambda v: np.floor(v + 0.5)}
for name, t, n in (('tab_x', tab[:320], 320), ('tab_y', tab[320:], 200)):
    x = np.arange(n)
    res = []
    for c in (n / 2 - 1, n / 2 - 0.5, n / 2):
        d = x - c
        for D in np.arange(60, 400, 1.0):
            for A in np.arange(100, 260, 0.5):
                v = 128 + A * d / np.sqrt(d * d + D * D)
                for rn, f in R.items():
                    e = np.abs(f(v) - t)
                    res.append((int(e.max()), int((e > 0).sum()), c, D, A, rn))
    res.sort()
    print(name, res[:4])
