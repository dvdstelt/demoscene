"""The ring table of bumpprm1.val against the unused generator at 0x402820, and the shape of bumptab.val.
usage: python3 fit_gen3.py"""
import math

import numpy as np

import bump

prm = bump.load('bump/bumpprm1.val')
t2 = np.frombuffer(prm[0x28:], np.int8).reshape(2048, 2).astype(int)
r0 = np.array([11 + math.trunc(11 * math.sin(2 * math.pi * 9 * i / 2048)) for i in range(2048)])
r1 = np.array([16 + math.trunc(16 * math.cos(2 * math.pi * 4 * i / 2048)) for i in range(2048)])
# the generator's own arithmetic: angle = i * N * (pi/128) * (1/8)
k = 0.024543692606     # [0x8203f4], printed with 12 digits; the double is pi/128
r0b = np.array([11 + math.trunc(11 * math.sin(i * (9 * (math.pi / 128)) * 0.125)) for i in range(2048)])
print('ring0 = 11 + trunc(11 sin(2pi 9 i / 2048)):', np.array_equal(r0, t2[:, 0]), (r0 != t2[:, 0]).sum(), 'differ')
print('ring1 = 16 + trunc(16 cos(2pi 4 i / 2048)):', np.array_equal(r1, t2[:, 1]), (r1 != t2[:, 1]).sum(), 'differ')
print('generator order of operations:', np.array_equal(r0b, t2[:, 0]))

tab = np.frombuffer(bump.load('bump/bumptab.val'), np.uint8).astype(float)
for name, t, n in (('tab_x', tab[:320], 320), ('tab_y', tab[320:], 200)):
    x = np.arange(n)
    w = 20
    sl = [(t[i + w] - t[i]) / w for i in range(0, n - w, w)]
    print(name, 'slope per %d px:' % w, np.round(sl, 3).tolist())
    print('  value at centre', t[n // 2 - 1:n // 2 + 2], 'ends', t[0], t[-1])
    best = None
    for f, fn in (('128+A*sin((x-c)*s)', lambda A, s, c: 128 + A * np.sin((x - c) * s)),
                  ('128+A*atan((x-c)*s)', lambda A, s, c: 128 + A * np.arctan((x - c) * s)),
                  ('128+A*tanh((x-c)*s)', lambda A, s, c: 128 + A * np.tanh((x - c) * s))):
        for A in np.arange(80, 260, 2.0):
            for s in np.geomspace(0.001, 0.02, 120):
                for c in (n / 2 - 1, n / 2 - 0.5, n / 2):
                    v = fn(A, s, c)
                    e = np.abs(v - t).max()
                    if best is None or e < best[0]:
                        best = (e, f, A, s, c)
    print('  best smooth fit (max err, form, A, s, c):', best)
