"""Exact formulas for the generated tables: the ring profiles in bumpprm1.val, the light spot phong.pcx / phong2.pcx,
the ramps in bumptab.val. usage: python3 fit_gen.py"""
import itertools

import numpy as np

import bump

prm = bump.load('bump/bumpprm1.val')
t2 = np.frombuffer(prm[0x28:], np.uint8).reshape(2048, 2).astype(int)
i = np.arange(2048)
rounders = {'floor': np.floor, 'round': lambda x: np.floor(x + 0.5), 'rint': np.rint, 'trunc': np.trunc, 'ceil': np.ceil}
for c, (amp, per) in enumerate(((11.0, 9), (16.0, 4))):
    for fname, f in (('sin', np.sin), ('cos', np.cos)):
        for rn, r in rounders.items():
            for ph in (0.0,):
                v = r(amp + amp * f(2 * np.pi * per * i / 2048 + ph)).astype(int)
                e = np.abs(v - t2[:, c])
                if e.max() <= 1:
                    print('ring%d = %s(%g + %g*%s(2pi*%d*i/2048)): max err %d, %d entries differ' % (
                        c, rn, amp, amp, fname, per, e.max(), (e > 0).sum()))
    # single precision?
    for fname, f in (('sin', np.sin), ('cos', np.cos)):
        for rn, r in rounders.items():
            x = (np.float32(2 * np.pi * per) * i.astype(np.float32) / np.float32(2048)).astype(np.float32)
            v = r(np.float32(amp) + np.float32(amp) * f(x).astype(np.float32)).astype(int)
            e = np.abs(v - t2[:, c])
            if e.max() <= 1:
                print('  float32: ring%d = %s(... %s): max err %d, %d differ' % (c, rn, fname, e.max(), (e > 0).sum()))

for n in ('bump/phong.pcx', 'bump/phong2.pcx'):
    px = bump.pcx(n)[0].astype(int)
    yy, xx = np.mgrid[0:256, 0:256]
    best = []
    for cx, cy in itertools.product((127.5, 128.0), (127.5, 128.0)):
        r = np.hypot(xx - cx, yy - cy)
        for k in (1.0, 127 / 128, 128 / 127):
            for base in (127.0, 127.5, 128.0):
                for rn, f in rounders.items():
                    v = np.clip(f(base - k * r), 0, 127).astype(int)
                    e = np.abs(v - px)
                    best.append((e.max(), (e > 0).sum(), cx, cy, k, base, rn))
    best.sort()
    print(n, 'best fits (maxerr, n differ, cx, cy, k, base, rounding):')
    for b in best[:4]:
        print('   ', b)
a, b = bump.load('bump/phong.pcx'), bump.load('bump/phong2.pcx')
print('phong.pcx pixels == phong2.pcx pixels', np.array_equal(bump.pcx('bump/phong.pcx')[0], bump.pcx('bump/phong2.pcx')[0]),
      'files equal', a == b, 'first differing byte', next((k for k in range(len(a)) if a[k] != b[k]), None))

tab = np.frombuffer(bump.load('bump/bumptab.val'), np.uint8).astype(int)
for name, t, n in (('tab_x', tab[:320], 320), ('tab_y', tab[320:], 200)):
    x = np.arange(n)
    res = []
    for s in np.linspace(0.6, 0.8, 2001):
        for cen in np.arange(n / 2 - 4, n / 2 + 4, 0.5):
            for rn, f in rounders.items():
                v = f(128 + (x - cen) * s).astype(int)
                e = np.abs(v - t)
                res.append((e.max(), (e > 0).sum(), round(s, 5), cen, rn))
    res.sort()
    print(name, 'best linear fits:', res[:3])
    d = np.diff(t)
    print('  steps', np.unique(d, return_counts=True))
