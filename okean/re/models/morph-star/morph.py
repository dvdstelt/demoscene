"""Model of the fourth part 0x367e5 (the morphing object): one frame from the callback count alone.

frame(c) = the draw buffer the main loop builds with the state after c retrace callbacks (c = 0 for the first frame)
and the noise of drawn frame c (in the capture: record 3474 + c, see notes-morph-star.md).

usage: python3 morph.py [c ...]   compares with the record that shows state c
"""
import sys

import numpy as np

from common import T, S, C, idiv, ror, u32, line_positions, add_blobs, open_capture, capture_frame

BLOB4 = [(0x1010100, 0x10101), (0x2020101, 0x1010202), (0x8040201, 0x1020408), (0x8060201, 0x1020608),
         (0x8060201, 0x1020608), (0x8040201, 0x1020408), (0x2020101, 0x1010202), (0x1010100, 0x10101)]


def edges():
    """set-up 0x3683a: 32 closed rings of 16 (512 edges), then for each of the 16 columns the 30 edges ring 1-2 ..
    ring 30-31 (480 edges); vertex numbers (the program stores them times 8)"""
    e = []
    b = 0
    for _ in range(32):
        for i in range(16):
            e.append((b + i, b + (i + 1) % 16))
        b = (b + 16) & 0x1ff
    for b in range(16):
        s = 16
        for _ in range(30):
            e.append((b + s, b + ((s + 16) & 0x1ff)))
            s = (s + 16) & 0x1ff
    return e


EDGES = edges()


def sphere():
    """set-up 0x368ba: 32 rings (latitude index 64 r, r = 0..31), 16 points each (longitude 256 k)"""
    v = []
    for r in range(32):
        cy = C(64 * r)
        sr = S(64 * r)
        for k in range(16):
            v.append(((S(256 * k) * sr) >> 10, cy, (C(256 * k) * sr) >> 10))
    return v


SPHERE = sphere()


def state(c):
    """the callback 0x36af4's variables after c callbacks (closed form)"""
    st = {}
    st['nedge'] = min(8 + 8 * c, 0x3e0)
    st['a60'] = ((T[14 * c & 0xfff] + T[11 * c & 0xfff]) >> 2) & 0xfff
    st['a64'] = 4 * c & 0xfff
    st['a68'] = ((T[5 * c & 0xfff] + T[12 * c & 0xfff]) >> 2) & 0xfff
    st['p4c'] = 0x1c * c & 0xfff
    st['p50'] = -0x19 * c & 0xfff
    st['zoff'] = T[7 * c & 0xfff] + 0xb90 if c else 0xb54
    return st


def noise_seeds(n, seed=0):
    """seed of drawn frames 0..n-1 (0x36943: 16000 steps per frame, x = ror(x + 0x9d, 11) ^ 0x9d)"""
    out = []
    x = seed
    for _ in range(n):
        out.append(x)
        for _ in range(16000):
            x = ror(x + 0x9d, 11) ^ 0x9d
    return out, x


def noise(seed):
    buf = np.zeros(16000, np.uint32)
    x = seed
    for i in range(16000):
        buf[i] = x & 0x0f0f0f0f
        x = ror(x + 0x9d, 11) ^ 0x9d
    return buf.view(np.uint8).copy(), x


def deform(st):
    """0x36978: ring r scale (T[p4c + 0x180 r] >> 3) + 0x200, column k adds T[p50 + 0x100 k] >> 4; v * s >> 9"""
    out = []
    for r in range(32):
        ed = (T[(st['p4c'] + 0x180 * r) & 0xfff] >> 3) + 0x200
        for k in range(16):
            s = (T[(st['p50'] + 0x100 * k) & 0xfff] >> 4) + ed
            x, y, z = SPHERE[16 * r + k]
            out.append(((x * s) >> 9, (y * s) >> 9, (z * s) >> 9))
    return out


def project(st, verts):
    """0x366bd: rotate xz by a64, yz by a60, xy by a68; z += zoff (<= 0 -> 1); X = (x << 8) / z + 160"""
    s64, c64 = S(st['a64']), C(st['a64'])
    s60, c60 = S(st['a60']), C(st['a60'])
    s68, c68 = S(st['a68']), C(st['a68'])
    out = []
    for x, y, z in verts:
        x1 = (x * c64 - s64 * z) >> 10
        z1 = (z * c64 + s64 * x) >> 10
        y1 = (y * c60 - s60 * z1) >> 10
        z2 = (z1 * c60 + s60 * y) >> 10
        x2 = (x1 * c68 - s68 * y1) >> 10
        y2 = (y1 * c68 + s68 * x1) >> 10
        zz = z2 + st['zoff']
        if zz <= 0:
            zz = 1
        out.append((idiv(x2 << 8, zz) + 160, idiv(y2 << 8, zz) + 100))
    return out


def frame(c, seed):
    st = state(c)
    buf, _ = noise(seed)
    pts = project(st, deform(st))
    pos = []
    for a, b in EDGES[:st['nedge']]:
        pos += line_positions(pts[a][0], pts[a][1], pts[b][0], pts[b][1])
    add_blobs(buf, pos, BLOB4)
    return buf.reshape(200, 320)


def main():
    """compare state c with its record: from p4_hooks.log (hookrun.py) when it exists, else record 3474 + c and
    noise frame c (true until the first dropped refresh, record 3543)"""
    import os
    import hooks
    cs = [int(x) for x in sys.argv[1:]] or [0, 1, 5, 50, 200]
    m = {x['cbs']: x for x in hooks.parse('p4_hooks.log')} if os.path.exists('p4_hooks.log') else {}
    pairs = [(c, m[c]['record'], m[c]['n']) if c in m else (c, 3474 + c, c) for c in cs]
    seeds, _ = noise_seeds(max(n for _, _, n in pairs) + 1)
    cap = open_capture()
    for c, rec, n in pairs:
        img = frame(c, seeds[n])
        ref, pal = capture_frame(cap, rec)
        d = img != ref
        print('c %d (noise frame %d) record %d: %d pixels differ' % (c, n, rec, d.sum()), np.argwhere(d)[:5].tolist())

if __name__ == '__main__':
    main()
