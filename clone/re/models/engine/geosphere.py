"""Model of the geosphere generator 0x4174c4 / 0x417420 / 0x4172c8 (octahedron 0x427f9c / 0x427f84, radius 300).

usage: python3 geosphere.py [../../snaps/t11.snap]   compares with the buffers 0xf61404 / 0xf6cf84 of a snapshot
"""
import pickle
import struct
import sys

import numpy as np

import fpu
from fpu import L

sys.path.insert(0, __file__.rsplit('/', 3)[0])          # clone/re for rep.py
import rep


def octahedron():
    """6 vertices (dwords, the low words are used) and 8 faces (bytes) stored in the exe."""
    verts = [struct.unpack('<3i', rep.rd(0x427f9c + 12 * i, 12)) for i in range(6)]
    faces = [tuple(rep.rd(0x427f84 + 3 * i, 3)) for i in range(8)]
    return verts, faces


def cdiv2(s):
    """C's (a + b) / 2 for ints: truncates toward zero (cdq; sub eax, edx; sar eax, 1)."""
    return -((-s) >> 1) if s < 0 else s >> 1


def geosphere(levels=4):
    fpu.chop()
    v0, f0 = octahedron()
    V = [tuple(v) for v in v0]
    F = []

    def midpoint(i, j):                                  # 0x4172c8
        a, b = V[i], V[j]
        mx, my, mz = (cdiv2(a[k] + b[k]) for k in range(3))
        s = L(my * my) + L(mx) * L(mx)                   # fild my^2 (int), fild mx, fst, fmul, faddp
        s = s + L(mz * mz)
        ln = fpu.fist(np.sqrt(s))                        # fsqrt (chopped), fistp (truncates)
        inv = L(1) / L(ln)
        p = (fpu.fist(L(mx) * L(300) * inv), fpu.fist(L(my) * L(300) * inv), fpu.fist(L(300) * L(mz) * inv))
        p = tuple(((c + 0x8000) & 0xffff) - 0x8000 for c in p)        # stored as words, compared as words
        for k, q in enumerate(V):
            if q == p:
                return k
        V.append(p)
        return len(V) - 1

    def sub(a, b, c, n):                                 # 0x417420 (the loop is the 4th, middle triangle)
        while n:
            ab = midpoint(a, b)
            bc = midpoint(b, c)
            n -= 1
            ca = midpoint(c, a)
            sub(a, ab, ca, n)
            sub(ab, b, bc, n)
            sub(ca, bc, c, n)
            a, b, c = ab, bc, ca
        F.append((a, b, c))

    for a, b, c in f0:
        sub(a, b, c, levels)
    return V, F


def main():
    V, F = geosphere(4)
    print('geosphere level 4: %d vertices, %d faces' % (len(V), len(F)))
    snap = sys.argv[1] if len(sys.argv) > 1 else __file__.rsplit('/', 3)[0] + '/snaps/t11.snap'
    import unicorn  # noqa: F401  (needed to unpickle the context)
    mem = pickle.load(open(snap, 'rb'))['mem']
    nv, nf = struct.unpack('<II', mem[0xf72d44:0xf72d4c])
    sv = [struct.unpack('<3h', mem[0xf61404 + 12 * i:0xf61404 + 12 * i + 6]) for i in range(nv)]
    sf = [struct.unpack('<3h', mem[0xf6cf84 + 6 * i:0xf6cf84 + 6 * i + 6]) for i in range(nf)]
    print('snapshot: %d vertices, %d faces' % (nv, nf))
    print('vertices identical:', sv == [tuple(v) for v in V], ' faces identical:', sf == [tuple(f) for f in F])
    r = np.array([np.sqrt(float(x * x + y * y + z * z)) for x, y, z in V])
    print('radius min %.3f max %.3f mean %.3f' % (r.min(), r.max(), r.mean()))


if __name__ == '__main__':
    main()
