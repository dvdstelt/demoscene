"""Model of the sixth part 0x3c8da (the tube of rings seen along its axis: the web and the star).

frame(c, seed) = the draw buffer the main loop builds after c retrace callbacks, with the noise seed of that drawn
frame (in the capture: record 5755 + c when no refresh was dropped; hookrun.py's log gives the exact mapping).

usage: python3 star.py [c ...]    compares with the record that shows state c
"""
import sys

import numpy as np

from common import T, S, C, rd32, rdbytes, idiv, rol, u32, line_positions, add_blobs

BLOB6 = [(0x2020200, 0x20202), (0x4040402, 0x2040404), (0x4040402, 0x2040404), (0x8040402, 0x2040408),
         (0x8040402, 0x2040408), (0x4040402, 0x2040404), (0x4040402, 0x2040404), (0x2020200, 0x20202)]

# static data in the file
EDGES = [(rdbytes(0x39c24 + 8 * i, 1)[0], rdbytes(0x39c24 + 8 * i + 4, 1)[0]) for i in range(0xf8)]
H0 = [rd32(0x39a04 + 4 * i) for i in range(136)]                 # radius history, 16 rings x 8 (+ 8 spare)
PH0 = [rd32(0x38dd0 + 4 * k) for k in range(8)]                  # ring-radius phases
PHINC = [0x29, 0x2b, 0x2c, 0x29, 0x2d, 0x2c, 0x2a, 0x2b]         # callback 0x3cad3, 0x3cc80..
A0 = [rd32(0x38dc0), rd32(0x38dc4), rd32(0x38dc8)]               # pitch phases (+0x10, -6, +0x17)
B0 = [rd32(0x38da4), rd32(0x38da8), rd32(0x38dac)]               # yaw phases (-0xd, +0x13, +8)
ZOFF = rd32(0x38db8)                                             # 0x400
SEED0 = rd32(0x38d1c, signed=False)                              # 0x8c25


def static_mesh():
    """set-up 0x3c8ef: 16 rings x 8 points at 0x39404 (vertices 128..255): radius 5 * 1022, angles 0x100 + 0x200 k,
    y = -0xc00 + 0x800 i. Projected every frame, never drawn (the edge list only uses vertices 0..127)."""
    v = []
    for i in range(16):
        for k in range(8):
            a = (0x100 + 0x200 * k) & 0xfff
            v.append((5 * S(a), -0xc00 + 0x800 * i, 5 * C(a)))
    return v


def state(c):
    """the callback's variables after c callbacks"""
    st = {}
    st['scroll'] = (-0x80 * c) & 0x3ff
    h = list(H0)
    for j in range(1, c + 1):                                       # ring shift at callbacks 1, 9, 17, ...
        if (-0x80 * j) & 0x3ff == 0x380:
            ph = [(PH0[k] + PHINC[k] * (j - 1)) & 0xfff for k in range(8)]
            h[128:136] = [(T[p] >> 1) + 0x400 for p in ph]
            h[0:128] = h[8:136]
    st['h'] = h
    a = [(A0[0] + 0x10 * c) & 0xfff, (A0[1] - 6 * c) & 0xfff, (A0[2] + 0x17 * c) & 0xfff]
    b = [(B0[0] - 0xd * c) & 0xfff, (B0[1] + 0x13 * c) & 0xfff, (B0[2] + 8 * c) & 0xfff]
    # main loop 0x3c9e7: angles from the phases
    st['A'] = (((((T[a[0]] >> 1) + T[a[1]]) >> 1) + T[a[2]]) >> 2) + 0x400 & 0xfff
    st['B'] = ((((T[b[0]] >> 1) + T[b[1]]) >> 1) + T[b[2]]) >> 2 & 0xfff
    return st


def fade_step(fade, order, row):
    """callback 0x3cb40: up to 16 outside order 22, down to 0 from order 22 row 32"""
    if order != 0x16:
        return fade + 1 if fade != 0x10 else fade
    if row >= 0x20 and fade != 0:
        return fade - 1
    return fade


def tube(st):
    """main loop 0x3c955: 16 rings x 8 points at 0x38e04: angle 0x200 k, radius h[8 i + k], y = -0x800 + 0x400 i"""
    v = []
    h = st['h']
    for i in range(16):
        for k in range(8):
            a = 0x200 * k
            r = h[8 * i + k]
            v.append(((T[a] * r) >> 8, -0x800 + 0x400 * i, (r * C(a)) >> 8))
    return v


def project(st, verts):
    """0x3c7b5: y += scroll; rotate yz by A, xz by B; z += 0x400; z > 0: X = (x << 6) / z + 160,
    else X = x * (1 - z) + 160 (same for Y + 100)"""
    sa, ca = S(st['A']), C(st['A'])
    sb, cb = S(st['B']), C(st['B'])
    out = []
    for x, y, z in verts:
        y += st['scroll']
        y1 = (y * ca - sa * z) >> 10
        z1 = (z * ca + sa * y) >> 10
        x1 = (x * cb - sb * z1) >> 10
        z2 = (z1 * cb + sb * x) >> 10
        zz = z2 + ZOFF
        if zz > 0:
            X, Y = idiv(x1 << 6, zz), idiv(y1 << 6, zz)
        else:
            X, Y = x1 * (1 - zz), y1 * (1 - zz)
        X = (X + 0xa0) & 0xffffffff
        Y = (Y + 0x64) & 0xffffffff
        out.append((X - (1 << 32) if X >= 1 << 31 else X, Y - (1 << 32) if Y >= 1 << 31 else Y))
    return out


def noise(seed):
    """0x3c9b4: x = seed; per dword: out = x & 0x1f1f1f1f; x = rol(x + 0x9d, 11) ^ 0x9d; seed += x"""
    buf = np.zeros(16000, np.uint32)
    x = seed
    s = seed
    for i in range(16000):
        buf[i] = x & 0x1f1f1f1f
        x = rol(x + 0x9d, 11) ^ 0x9d
        s = (s + x) & 0xffffffff
    return buf.view(np.uint8).copy(), s


def noise_seeds(n, seed=SEED0):
    out = []
    for _ in range(n):
        out.append(seed)
        _, seed = noise(seed)
    return out


def frame(c, seed):
    st = state(c)
    buf, _ = noise(seed)
    pts = project(st, tube(st))
    pos = []
    for a, b in EDGES:
        pos += line_positions(pts[a][0], pts[a][1], pts[b][0], pts[b][1], reject_x=True)
    add_blobs(buf, pos, BLOB6)
    return buf.reshape(200, 320)


def main():
    """compare state c with its record: from p6_hooks.log (hookrun.py) when it exists, else record 5755 + c and
    noise frame c (true until the first dropped refresh, record 5806)"""
    import os
    import hooks
    from common import open_capture, capture_frame, caption
    cs = [int(x) for x in sys.argv[1:]] or [0, 1, 5, 40]
    m = {x['cbs']: x for x in hooks.parse('p6_hooks.log')} if os.path.exists('p6_hooks.log') else {}
    pairs = [(c, m[c]['record'], m[c]['n']) if c in m else (c, 5755 + c, c) for c in cs]
    seeds = noise_seeds(max(n for _, _, n in pairs) + 1)
    cap = open_capture()
    for c, rec, n in pairs:
        img = frame(c, seeds[n])
        if c in m and m[c]['pos'].startswith('22:') and int(m[c]['pos'][3:]) < 16:
            caption(img, 'the sea robot of love')               # the shared printer (notes 3.6)
        ref, pal = capture_frame(cap, rec)
        d = img != ref
        print('c %d (noise frame %d) record %d: %d pixels differ' % (c, n, rec, d.sum()), np.argwhere(d)[:5].tolist())


if __name__ == '__main__':
    main()
