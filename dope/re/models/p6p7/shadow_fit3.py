"""p6: the shadow masks look like the two rings projected orthographically onto the screen plane (ring 0 edge-on at
mask 0, both at 45 degrees around mask 16). Fit: mask pixel (i, j) -> plane point ((i - cx) s, (cy - j) s); ring k
(axis x or z, turned by phi about the vertical) projects to the curve (R cos t cos(phi_k), R sin t); shadow where the
distance to the curve < rt. Prints the best agreement over all 32 masks."""
import math
import numpy as np
from snap import img

b = img('p6')
PAT = np.frombuffer(b, np.uint8, 32 * 4096, 0x368a * 16).reshape(32, 64, 64) & 1
R, RT = 7884.0, 459.0
T = np.linspace(0, 2 * math.pi, 720, endpoint=False)
j, i = np.mgrid[0:64, 0:64]


def mask(phi, cx, cy, s):
    qx, qy = (i - cx) * s, (cy - j) * s
    sh = np.zeros((64, 64), bool)
    for ph in (phi, phi + math.pi / 2):
        px, py = R * np.cos(T) * math.cos(ph), R * np.sin(T)
        d2 = (qx[..., None] - px) ** 2 + (qy[..., None] - py) ** 2
        sh |= d2.min(-1) < RT * RT
    return (~sh).astype(np.uint8)


def score(cx, cy, s, sgn, off, ks=range(0, 32, 4)):
    return np.mean([(mask(sgn * (k + off) * math.pi / 64, cx, cy, s) == PAT[k]).mean() for k in ks])


best = None
for s in (160, 170, 175, 180, 190):
    for cx in (30.5, 31.5, 32.5, 34.5):
        for sgn in (1, -1):
            for off in (0, 0.5, 1):
                sc = score(cx, 31.5, s, sgn, off)
                if best is None or sc > best[0]:
                    best = (sc, s, cx, sgn, off)
print('coarse best', best, flush=True)
sc, s, cx, sgn, off = best
for s2 in np.arange(s - 8, s + 9, 2):
    for cx2 in np.arange(cx - 1.5, cx + 1.6, 0.5):
        for cy2 in (30.5, 31.5, 32.5):
            for off2 in (off - 0.5, off, off + 0.5):
                sc = score(cx2, cy2, s2, sgn, off2)
                if sc > best[0]:
                    best = (sc, s2, cx2, sgn, off2, cy2)
print('refined best', best)
print('all 32 masks with the best parameters: agreement %.4f' % score(best[2], best[5] if len(best) > 5 else 31.5,
                                                                      best[1], best[3], best[4], range(32)))
print('all-lit baseline %.3f' % PAT.mean())
