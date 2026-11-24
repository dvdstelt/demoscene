"""Fit smooth radial profiles to the 8x8 stamp of 0x52fed (centre between pixels 3 and 4), and the cross-section of
an axis-aligned and a diagonal line made of stamps every 2 px along the major axis."""
import numpy as np
K = np.array([[0, 1, 1, 1, 1, 1, 1, 0], [1, 1, 2, 2, 2, 2, 1, 1], [1, 2, 4, 8, 8, 4, 2, 1], [1, 2, 8, 15, 15, 8, 2, 1],
              [1, 2, 8, 15, 15, 8, 2, 1], [1, 2, 4, 8, 8, 4, 2, 1], [1, 1, 2, 2, 2, 2, 1, 1], [0, 1, 1, 1, 1, 1, 1, 0]],
             float)
yy, xx = np.mgrid[0:8, 0:8] + 0.5
d2 = (xx - 4) ** 2 + (yy - 4) ** 2
best = None
for s in np.linspace(0.5, 3, 251):
    for kind in ('gauss', 'exp'):
        f = np.exp(-d2 / (2 * s * s)) if kind == 'gauss' else np.exp(-np.sqrt(d2) / s)
        A = (f * K).sum() / (f * f).sum()
        err = np.sqrt(((A * f - K) ** 2).mean())
        if best is None or err < best[0]:
            best = (err, kind, s, A)
        if kind == 'gauss' and abs(s - 1.2) < 1e-9:
            pass
print('best single fit: rms %.2f %s scale %.2f amplitude %.1f' % best)
for kind in ('gauss', 'exp'):
    errs = []
    for s in np.linspace(0.5, 3, 251):
        f = np.exp(-d2 / (2 * s * s)) if kind == 'gauss' else np.exp(-np.sqrt(d2) / s)
        A = (f * K).sum() / (f * f).sum()
        errs.append((np.sqrt(((A * f - K) ** 2).mean()), s, A))
    print(kind, 'best rms %.2f s %.2f A %.1f' % min(errs))
# gaussian + constant floor
errs = []
for s in np.linspace(0.5, 3, 251):
    f = np.exp(-d2 / (2 * s * s))
    M = np.stack([f.ravel(), np.ones(64)], 1)
    (A, B), *_ = np.linalg.lstsq(M, K.ravel(), rcond=None)
    errs.append((np.sqrt(((A * f + B - K) ** 2).mean()), s, A, B))
print('gauss + floor best rms %.2f s %.2f A %.1f B %.2f' % min(errs))
# cross sections
img = np.zeros((40, 80))
for i in range(30):
    x = 4 + 2 * i
    img[10:18, x:x + 8] += K
print('horizontal line, column 40 rows 10..17:', img[10:18, 40])
img = np.zeros((90, 90))
for i in range(30):
    x = y = 4 + 2 * i
    img[y:y + 8, x:x + 8] += K
row = img[45, 30:60]
print('diagonal line, row 45 cols 38..53:', img[45, 38:54], 'sum across', img[45, :].sum(), 'vs horizontal', 100.0)
