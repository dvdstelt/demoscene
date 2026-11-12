"""Continuous form of the cloud's table: which radius and angle each pixel got, against the pixel-centre geometry."""
import numpy as np
from cloud_model import SIN
from common import out

Rmap = np.full(64000, -1); Kmap = np.full(64000, -1)
for n in range(1, 200):
    R = n + 1
    k = np.arange(2200)
    y = ((SIN[k] * R) >> 14) + 100
    x = ((-SIN[(k + 0x226) % 0x898] * R) >> 14) + 160
    ok = (y >= 0) & (y < 200) & (x >= 0) & (x < 320)
    p = (y * 320 + x)[ok]; kk = k[ok]
    # first hit in k order within this radius, and only pixels not hit by a smaller radius
    order = np.argsort(p, kind='stable')
    p_s, k_s = p[order], kk[order]
    if len(p_s) == 0: continue
    first = np.r_[True, p_s[1:] != p_s[:-1]]
    p_s, k_s = p_s[first], k_s[first]
    new = Rmap[p_s] < 0
    Rmap[p_s[new]] = R; Kmap[p_s[new]] = k_s[new]
T = np.load(out('cloud_table.npy'))
hit = Rmap >= 0
py, px = np.divmod(np.arange(64000), 320)
for ox, oy in [(0.5, 0.5), (0.0, 0.0), (1.0, 1.0), (0.5, 1.0), (1.0, 0.5)]:
    dx = px + ox - 160; dy = py + oy - 100
    d = np.hypot(dx, dy)
    r = (Rmap - d)[hit & (d > 5)]
    print('pixel point (+%.1f, +%.1f): R - d mean %.3f sd %.3f min %.2f max %.2f' % (ox, oy, r.mean(), r.std(), r.min(), r.max()))
dx = px + 0.5 - 160; dy = py + 0.5 - 100
theta = np.arctan2(dy, -dx) % (2 * np.pi)
kth = Kmap * 2 * np.pi / 2200
e = (kth - theta + np.pi) % (2 * np.pi) - np.pi
m = hit & (np.hypot(dx, dy) > 5)
print('angle (k * 2pi/2200) - atan2(dy, -dx): mean %.4f sd %.4f rad' % (e[m].mean(), e[m].std()))
# depth byte vs 5120/(R+18)
dep = T & 255
print('depth == (5120 // (R + 18)) & 255:', bool((dep[hit] == (5120 // (Rmap[hit] + 18)) & 255).all()))
ang = T >> 8
print('angle byte == (k * 0x1dca) >> 16:', bool((ang[hit] == ((Kmap[hit] * 0x1dca) >> 16)).all()))
