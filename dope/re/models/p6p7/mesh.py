"""Read p6 / p7 meshes from the unpacked image.

Mesh header (words): [0] vertex count, [2] face count, [4] offset of vertex positions (3 x s16 each),
[6] offset of faces (8 bytes: u16 material (lo = env-map selector, hi = bump selector), 3 x u16 vertex index*4),
[8] offset of vertex normals (3 x s16), [0xa] offset of per-vertex texture coordinates (u16 u, u16 v).
"""
import struct
import sys

import numpy as np

from snap import img


def load(part, seg):
    b = img(part)
    base = seg * 16
    h = struct.unpack_from('<8H', b, base)
    nv, nf, ov, of, on, ot = h[:6]
    V = np.frombuffer(b, '<i2', nv * 3, base + ov).reshape(nv, 3)
    F = np.frombuffer(b, '<u2', nf * 4, base + of).reshape(nf, 4)
    N = np.frombuffer(b, '<i2', nv * 3, base + on).reshape(nv, 3)
    T = np.frombuffer(b, '<u2', nv * 2, base + ot).reshape(nv, 2)
    return dict(h=h, V=V, F=F, N=N, T=T)


if __name__ == '__main__':
    part = sys.argv[1]
    for s in sys.argv[2:]:
        m = load(part, int(s, 16))
        print(s, 'header', [hex(x) for x in m['h']])
        V, F, N, T = m['V'], m['F'], m['N'], m['T']
        print(' verts', len(V), 'min', V.min(0), 'max', V.max(0))
        print(' faces', len(F), 'materials', {hex(k): int(v) for k, v in zip(*np.unique(F[:, 0], return_counts=True))})
        print(' vertex refs: max', F[:, 1:].max() // 4, 'all multiple of 4', bool((F[:, 1:] % 4 == 0).all()))
        nl = np.sqrt((N.astype(float) ** 2).sum(1))
        print(' normal length min/max/mean', nl.min(), nl.max(), nl.mean())
        print(' tex u min/max', T[:, 0].min(), T[:, 0].max(), ' v min/max', T[:, 1].min(), T[:, 1].max())
        print(' first verts', V[:3].tolist(), 'normals', N[:3].tolist(), 'tex', [hex(x) for x in T[:3].ravel()])
