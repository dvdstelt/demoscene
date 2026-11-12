"""p6: are the 32 shadow masks (368a) the shadows of the two rings on a sphere at their centre, lit from the
direction of the light map's highlight, as seen in env-map (normal) space?  Brute-force fit of the sphere radius,
the tilt angle a and the angle offset; prints the agreement with the stored masks."""
import math
import numpy as np
from snap import img

b = img('p6')
PAT = np.frombuffer(b, np.uint8, 32 * 4096, 0x368a * 16).reshape(32, 64, 64) & 1
K = 53.82
j, i = np.mgrid[0:64, 0:64]
row, col = 2 * j + 1.0, 2 * i + 1.0          # env texel centres covered by mask pixel (j, i)
nx = (64 - col) / K                           # camera space: x = screen left, y = up, z = towards the viewer
ny = (64 - row) / K
rr = nx ** 2 + ny ** 2
inside = rr < 0.98
nz = np.sqrt(np.clip(1 - rr, 0, 1))
N = np.stack([nx, ny, nz], -1)


def rot(a, bb, c):
    s1, c1 = math.sin(a), math.cos(a); s2, c2 = math.sin(bb), math.cos(bb); s3, c3 = math.sin(c), math.cos(c)
    return np.array([[c2 * c3 - s2 * s1 * s3, c1 * s3, s2 * c3 + c2 * s1 * s3],
                     [-(c2 * s3 + s2 * s1 * c3), c1 * c3, -(s2 * s3 - c2 * s1 * c3)],
                     [-s2 * c1, -s1, c2 * c1]])


def shadow(M, r, L, R0=7884.0, rt=459.0):
    P = N * r                                   # surface points
    sh = np.zeros(N.shape[:2], bool)
    for axis in (np.array([1.0, 0, 0]), np.array([0, 0, 1.0])):
        A = M @ axis                            # ring axis in camera space
        # sample the ray s + t L, t in [0, 3 R0]; distance to the circle |p.A|, radial = |p - (p.A)A|
        for t in np.linspace(0, 2.5 * R0, 400):
            p = P + t * L
            h = p @ A
            rad = np.sqrt(np.maximum((p * p).sum(-1) - h * h, 0))
            d = np.sqrt(h * h + (rad - R0) ** 2)
            sh |= d < rt
    return sh


if __name__ == "__main__":
    Lh = np.array([(64 - 92) / K, (64 - 92) / K, 0])
    Lh[2] = math.sqrt(1 - Lh[0] ** 2 - Lh[1] ** 2)
    best = None
    if __name__ != "__main__":
        pass
    for r in (1500, 2500, 3500, 4500):
        for a in (0.0,):
            for sgn in (1, -1):
                score = 0
                for idx in (0, 8, 16, 24):
                    bb = sgn * (idx + 0.5) * 512 / 65536 * 2 * math.pi
                    sh = shadow(rot(a, bb, 0), r, Lh)
                    score += ((~sh).astype(int) == PAT[idx])[inside].mean()
                score /= 4
                print('r %5d a %.2f sign %+d agreement %.3f' % (r, a, sgn, score), flush=True)
                if best is None or score > best[0]:
                    best = (score, r, a, sgn)
    print('best', best, ' (all-lit baseline %.3f)' % np.mean([PAT[k][inside].mean() for k in (0, 8, 16, 24)]))
