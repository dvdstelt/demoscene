"""Model of the matrix / vector routines 0x404c9f, 0x404cd6, 0x404d5c, 0x404e36, 0x404f0f, 0x404feb, 0x4050e9,
checked against the routines themselves (run in unicorn by emu.py) on random inputs.

Matrix = 12 floats, 3 rows of 4: row i = (m[4i], m[4i+1], m[4i+2], m[4i+3]); p' = M (x, y, z, 1).
usage: python3 matrix.py
"""
import random

import numpy as np

import fpu
from fpu import L, f32
from emu import Emu


def identity():                          # 0x404c9f
    m = [L(0)] * 12
    m[0] = m[5] = m[10] = L(1)
    return m


def scale(m, sx, sy, sz):                # 0x404cd6 (stack: sx, sy, sz): row i *= s_i (incl. the translation)
    s = (f32(sx), f32(sy), f32(sz))
    return [f32(m[i] * s[i // 4]) for i in range(12)]


def _rot(m, ang, a, b):
    """rows a, b := (c row_a - s row_b, c row_b + s row_a), s and c stored as floats first."""
    s, c = f32(fpu.fsin(f32(ang))), f32(fpu.fcos(f32(ang)))
    m = list(m)
    for k in range(4):
        ra, rb = m[4 * a + k], m[4 * b + k]
        m[4 * a + k] = f32(ra * c - rb * s)
        m[4 * b + k] = f32(rb * c + ra * s)
    return m


def rot_z(m, ang):                       # 0x404d5c: rows 0, 1  -> M = Rz M, Rz = [[c,-s,0],[s,c,0],[0,0,1]]
    return _rot(m, ang, 0, 1)


def rot_y(m, ang):                       # 0x404e36: rows 2, 0  -> M = Ry M, Ry = [[c,0,s],[0,1,0],[-s,0,c]]
    return _rot(m, ang, 2, 0)


def rot_x(m, ang):                       # 0x404f0f: rows 1, 2  -> M = Rx M, Rx = [[1,0,0],[0,c,-s],[0,s,c]]
    return _rot(m, ang, 1, 2)


def translate(m, tx, ty, tz):            # 0x404feb: m3 += tx, m7 += ty, m11 += tz
    m = list(m)
    m[3], m[7], m[11] = f32(m[3] + f32(tx)), f32(m[7] + f32(ty)), f32(m[11] + f32(tz))
    return m


def transform(m, pts):                   # 0x4050e9: dst = ((m1 y + m0 x) + m2 z) + m3, per row, stored as floats
    out = []
    for x, y, z in pts:
        x, y, z = L(x), L(y), L(z)
        out.append(tuple(f32(m[4 * r + 1] * y + m[4 * r] * x + m[4 * r + 2] * z + m[4 * r + 3]) for r in range(3)))
    return out


def main():
    fpu.chop()
    e = Emu()
    A, P, Q = 0x7a000, 0x7b000, 0x7c000
    rnd = random.Random(1)
    bad = 0
    for trial in range(300):
        m0 = [np.float32(rnd.uniform(-3, 3)) for _ in range(12)]
        ang = np.float32(rnd.uniform(-20, 20))
        cases = [('identity', 0x404c9f, (), lambda m: identity()),
                 ('rot_z', 0x404d5c, (float(ang),), lambda m: rot_z(m, ang)),
                 ('rot_y', 0x404e36, (float(ang),), lambda m: rot_y(m, ang)),
                 ('rot_x', 0x404f0f, (float(ang),), lambda m: rot_x(m, ang)),
                 ('scale', 0x404cd6, (1.0, 1.3333334, -0.5), lambda m: scale(m, np.float32(1.0),
                                                                            np.float32(1.3333334), np.float32(-0.5))),
                 ('translate', 0x404feb, (10.5, -3.25, 2000.0), lambda m: translate(m, np.float32(10.5),
                                                                                np.float32(-3.25), np.float32(2000)))]
        for name, addr, args, model in cases:
            e.wfloats(A, m0)
            e.call(addr, eax=A, stack=args)
            got = e.floats(A, 12)
            want = np.array([np.float32(v) for v in model([L(v) for v in m0])], np.float32)
            if not np.array_equal(got.view(np.uint32), want.view(np.uint32)):
                bad += 1
                if bad < 5:
                    print(name, 'differs', got, want)
        pts = [tuple(np.float32(rnd.uniform(-400, 400)) for _ in range(3)) for _ in range(20)]
        src = np.zeros((20, 13), np.float32)
        src[:, :3] = pts
        e.wfloats(A, m0)
        e.wr(P, src.tobytes())
        e.call(0x4050e9, eax=A, edx=P, ebx=Q, ecx=20)
        got = e.floats(Q, 20 * 13).reshape(20, 13)[:, :3]
        want = np.array([[np.float32(c) for c in p] for p in transform([L(v) for v in m0], pts)], np.float32)
        if not np.array_equal(got.view(np.uint32), want.view(np.uint32)):
            bad += 1
            print('transform differs')
    print('300 random cases x 7 routines: %d differences' % bad)


if __name__ == '__main__':
    main()
