"""Continuous equivalents of the integer zoom maps of the particles (120/128) and the dissolve (119/128): least-squares
fit of src_centre = a * dst_centre + b per axis (pixel centres at +0.5), the fixed point (expansion centre)
b / (1 - a), and the largest deviation of the integer map from the fit."""
import numpy as np


def fit(src, n):
    d = np.arange(n) + 0.5; s = np.asarray(src) + 0.5
    a, b = np.polyfit(d, s, 1)
    return a, b, b / (1 - a), np.abs(s - (a * d + b)).max()


for name, k in (('particles', 0x78), ('dissolve', 0x77)):
    cols = [((e * k + 0x40) >> 7) + 0xa0 for e in range(-0xa0, 0xa0, 4)]
    xs = [cols[x // 4] + x % 4 for x in range(320)]
    ys = [((r - 100) * k >> 7) + 100 for r in range(200)]       # = ROWS[199 - r]
    for ax, v, n in (('x', xs, 320), ('y', ys, 200)):
        a, b, c, dev = fit(v, n)
        print('%-9s %s: src = %.5f * dst + %.3f   (nominal scale %.5f)   centre %.2f   max deviation %.2f px'
              % (name, ax, a, b, k / 128, c, dev))
# dissolve offsets: dx = floor(1023 sin / 256) + 1, dy = floor(1023 cos / 256): their mean over a turn
S = np.round(1023 * np.sin(2 * np.pi * np.arange(2048) / 2048)).astype(int)
dx = (S * 4 >> 10) + 1; dy = (np.roll(S, -512) * 4 >> 10)
print('dissolve offsets: dx %d..%d mean %.3f, dy %d..%d mean %.3f; fit dx = %.3f sin + %.3f'
      % (dx.min(), dx.max(), dx.mean(), dy.min(), dy.max(), dy.mean(),
         *np.linalg.lstsq(np.c_[np.sin(2 * np.pi * np.arange(2048) / 2048), np.ones(2048)], dx, rcond=None)[0]))
