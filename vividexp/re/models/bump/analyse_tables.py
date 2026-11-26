"""Fit the static tables: bumptab.val (linear ramps?), bumpwave.val (8 x distance?), bumpprm1.val's two ring
profiles, and whether the light pictures are radial functions. usage: python3 analyse_tables.py"""
import numpy as np

import bump

tab = np.frombuffer(bump.load('bump/bumptab.val'), np.uint8).astype(float)
tx, ty = tab[:320], tab[320:]
for name, t in (('tab_x', tx), ('tab_y', ty)):
    n = len(t)
    x = np.arange(n)
    k, c = np.polyfit(x, t, 1)
    print(name, 'first', t[:6], 'last', t[-6:], 'fit %.5f*x + %.4f' % (k, c), 'max resid', np.abs(t - (k * x + c)).max())
    for off in (0, 0.5):
        for f in (np.floor, np.round, np.trunc):
            pass
    # try 128 + (x - centre) * s, floor / round
    best = None
    for s in np.linspace(0.5, 0.9, 4001):
        for cen in np.arange(n / 2 - 20, n / 2 + 20, 0.5):
            for fn in (np.floor, np.round):
                v = fn(128 + (x - cen) * s)
                e = np.abs(v - t).sum()
                if best is None or e < best[0]:
                    best = (e, s, cen, fn.__name__)
    print('  best 128 + (x - c) * s:', best)

src = np.frombuffer(bump.load('bump/bumpwave.val'), '<u2').reshape(200, 320).astype(float)
yy, xx = np.mgrid[0:200, 0:320]
for cx, cy in ((319, 199), (319.5, 199.5), (320, 200)):
    d = np.hypot(xx - cx, yy - cy)
    for k in (8.0,):
        print('wave src vs %g*dist from (%g,%g): max err %.3f (floor %d, round %d)' % (
            k, cx, cy, np.abs(src - k * d).max(), np.abs(np.floor(k * d) - src).max(), np.abs(np.round(k * d) - src).max()))
print('src corners', src[0, 0], src[0, 319], src[199, 0], src[199, 319], 'max', src.max())
r = np.hypot(xx - 319, yy - 199)
ratio = src[r > 5] / r[r > 5]
print('src/dist from (319,199): min %.4f max %.4f' % (ratio.min(), ratio.max()))

prm = bump.load('bump/bumpprm1.val')
t2 = np.frombuffer(prm[0x28:], np.uint8).reshape(2048, 2).astype(float)
for c in range(2):
    p = t2[:, c]
    print('ring%d min %d max %d mean %.3f' % (c, p.min(), p.max(), p.mean()), 'argmax', p.argmax(), 'argmin', p.argmin())
    i = np.arange(2048)
    # fit a + b*sin(2 pi i / P + ph)
    best = None
    for P in (2048, 1024, 512, 256, 128):
        for ph in np.linspace(0, 2 * np.pi, 721):
            s = np.sin(2 * np.pi * i / P + ph)
            A = np.vstack([np.ones_like(s), s]).T
            co, res, *_ = np.linalg.lstsq(A, p, rcond=None)
            e = np.abs(A @ co - p).max()
            if best is None or e < best[0]:
                best = (e, P, ph, co)
    print('  best sine fit: maxerr %.3f period %d phase %.4f offset %.3f amp %.3f' % (best[0], best[1], best[2], *best[3]))
    print('  first 40', p[:40].astype(int).tolist())
    print('  every 64th', p[::64].astype(int).tolist())

for n in ('bump/phong.pcx', 'bump/phong2.pcx'):
    px = bump.pcx(n)[0].astype(float)
    yy, xx = np.mgrid[0:256, 0:256]
    best = None
    for cx in np.arange(126, 130.5, 0.5):
        for cy in np.arange(126, 130.5, 0.5):
            rr = np.round(np.hypot(xx - cx, yy - cy)).astype(int)
            spread = 0
            for v in range(0, 130):
                m = rr == v
                if m.any():
                    spread = max(spread, px[m].max() - px[m].min())
            if best is None or spread < best[0]:
                best = (spread, cx, cy)
    print(n, 'centre', np.unravel_index(px.argmax(), px.shape), 'max', px.max(), 'radial spread (best centre)', best)
    cx, cy = best[1], best[2]
    rr = np.hypot(xx - cx, yy - cy)
    prof = [px[(rr >= v - 0.5) & (rr < v + 0.5)].mean() for v in range(0, 182, 6)]
    print('  radial profile every 6 px:', [round(v, 1) for v in prof])
    print('  row 128:', px[128, ::8].astype(int).tolist())
    print('  col 128:', px[::8, 128].astype(int).tolist())
