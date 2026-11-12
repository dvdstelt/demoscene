"""p6: second try at explaining the shadow masks (see shadow_fit.py): surface points on a flat disc (z = 0) or on a
sphere, light along the view axis or along the highlight direction (with mirror variants); agreement over the whole
64x64 mask for 4 of the 32 masks."""
import math
import numpy as np
import shadow_fit as sf

j, i = np.mgrid[0:64, 0:64]
nx = (64 - (2 * i + 1.0)) / sf.K
ny = (64 - (2 * j + 1.0)) / sf.K
ALL = np.ones(nx.shape, bool)


def shadow(M, P, L, R0=7884.0, rt=459.0):
    sh = np.zeros(P.shape[:2], bool)
    for axis in (np.array([1.0, 0, 0]), np.array([0, 0, 1.0])):
        A = M @ axis
        for t in np.linspace(-2.5 * R0, 2.5 * R0, 600):
            p = P + t * L
            h = p @ A
            rad = np.sqrt(np.maximum((p * p).sum(-1) - h * h, 0))
            sh |= np.sqrt(h * h + (rad - R0) ** 2) < rt
    return sh


res = []
for surf in ('flat', 'sphere'):
    for Lname, L in (('view', np.array([0, 0, 1.0])), ('hl', np.array([-0.52, -0.52, 0.677])),
                     ('hl-mx', np.array([0.52, -0.52, 0.677])), ('hl-my', np.array([-0.52, 0.52, 0.677])),
                     ('hl-mxy', np.array([0.52, 0.52, 0.677]))):
        for r in (4000, 6000, 8000, 10000):
            nz = np.sqrt(np.clip(1 - nx ** 2 - ny ** 2, 0, 1)) if surf == 'sphere' else 0 * nx
            P = np.stack([nx, ny, nz], -1) * r
            for sgn in (1, -1):
                sc = 0
                for idx in (0, 8, 16, 24):
                    bb = sgn * (idx + 0.5) * 512 / 65536 * 2 * math.pi
                    sh = shadow(sf.rot(0.0, bb, 0), P, L)
                    sc += ((~sh).astype(int) == sf.PAT[idx]).mean()
                res.append((sc / 4, surf, Lname, r, sgn))
                print('%-6s %-6s r %5d sign %+d agreement %.3f' % (surf, Lname, r, sgn, sc / 4), flush=True)
res.sort(reverse=True)
print('best', res[:3], 'all-lit baseline %.3f' % np.mean([sf.PAT[k].mean() for k in (0, 8, 16, 24)]))
