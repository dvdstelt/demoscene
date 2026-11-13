"""Models of the grids that parts 2 (0x4198bf), 4 (0x4037c0) and 5 (0x403530) build for the filler, and of their
per-frame state updates. Checked against the frames dumped by models/grid/run_clone.py --dump.

usage: python3 models/grid/gridparts.py [2 4 5 | 5:dump/grid/p5slow.pkl ...]     (from clone/re)
For every dumped frame: the state update (entry state + dt -> state at the filler call) and the 41 x 21 grid
computed from the state, compared with the original's (u and v in 1/256 texel, light in 1/256 of a table step).
"""
import math
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.normpath(os.path.join(HERE, '..', '..', 'dump', 'grid'))


def f32t(x):
    """float -> float32 with the original's rounding mode (truncate toward zero)."""
    f = np.float32(x)
    if abs(float(f)) > abs(x):
        f = np.nextafter(f, np.float32(0))
    return float(f)


def trunc(x):
    return int(math.trunc(x))


def F(d, a):
    return struct.unpack('<f', d[a][:4])[0]


def I(d, a):
    return struct.unpack('<i', d[a][:4])[0]


def xy(c, r):
    """screen direction of grid entry (c, r): pixel (8c, 20 + 8r), focal length 120 pixels."""
    return f32t((c * 8.0 - 160.0) * (1 / 120)), f32t((8.0 * r - 80.0) * (1 / 120))


def rotate(xs, ys, a1, a2, a3):
    """the rotation shared by parts 2 and 4: by a3 in x-y, by a2 in x-z (z = 1), by a1 in y-z."""
    x1 = f32t(math.sin(a3) * ys + math.cos(a3) * xs)
    y1 = f32t(math.cos(a3) * ys - math.sin(a3) * xs)
    z2 = f32t(math.sin(a2) * x1 + math.cos(a2))
    x2 = f32t(math.cos(a2) * x1 - math.sin(a2))
    y3 = f32t(math.sin(a1) * z2 + math.cos(a1) * y1)
    z3 = f32t(math.cos(a1) * z2 - math.sin(a1) * y1)
    return x2, y3, z3


def cylinder(da, db, pa, pb):
    """forward distance from (pa, pb) along (da, db) to the circle of radius 192 (0x41a248 = -36864)."""
    a = f32t(da * da + db * db)
    b = f32t((pa * da + pb * db) * 2.0)
    disc = b * b - (da * da + db * db) * 4.0 * (pa * pa + pb * pb - 36864.0)
    s = math.sqrt(disc)
    t1 = f32t((s - b) / (a * 2.0 + 0.001))
    t2 = f32t((-b - s) / (a * 2.0 + 0.001))
    return t1 if t1 > 0 else t2


def word(x):
    return x & 0xffff


def grid_part5(T, R):
    g = np.zeros((21, 41, 3), np.int64)
    cR, sR = math.cos(R), math.sin(R)
    for r in range(21):
        for c in range(41):
            X, Y = (c - 20) * 0x83400, (r - 10) * 0x83400
            Xr = f32t(cR * X - sR * Y)
            Yr = f32t(sR * X + cR * Y)
            S = math.sin(0.296 * r + T * 7.0) * (130.0 * math.sin(0.141 * c + T * 4.0)) + \
                math.sin(0.176 * r + T * 8.0) * (55.0 * math.sin(0.261 * c + T * 6.0))
            Z = f32t(S * math.sin(T * 5.0) + 200.0)
            u = trunc(Xr * (1.0 / Z) + 32768.0)
            v = trunc(f32t((1.0 / Z) * Yr) + 32768.0)
            light = trunc(Z * 32768.0 * 0.0025 + 18432.0)
            g[r, c] = word(u), word(v), word(light)
    return g


def grid_part2(TCo, TCn, Br, flag):
    if flag:
        Ox, Oz = f32t(110.0 * math.sin(TCo * 0.013 + 2.0)), f32t(TCo * 3.0)
    else:
        Ox, Oz = f32t(150.0 * math.sin(TCo * 0.01)), f32t(TCo * 5.0)
    Oy = 0.0
    r1, r2, r3 = f32t(TCn * 0.01), f32t(TCn * 0.012), f32t(TCn * 0.008)
    g = np.zeros((21, 41, 3), np.int64)
    for r in range(21):
        for c in range(41):
            xs, ys = xy(c, r)
            x2, y3, z3 = rotate(xs, ys, r1, r2, r3)
            n = math.sqrt(x2 * x2 + y3 * y3 + z3 * z3)
            dx, dy, dz = f32t(1 / n * 0.17 * x2), f32t(y3 / n), f32t(z3 / n)
            t = cylinder(dx, dy, Ox, Oy)
            hx, hy, hz = dx * t + Ox, dy * t + Oy, dz * t + Oz
            u = trunc(hz * 0.6 * 256.0 * 0.5 * 0.25)
            v = trunc(math.atan2(hx, hy) * 256.0 * (1 / math.pi) * 256.0)
            Lf = f32t(53.0 - t * 0.024 * (1 / 256) * 192.0)
            if not Lf >= 16.0: Lf = 16.0
            Lf = f32t(Lf * 878.0)
            if Lf > 64535.0: Lf = 64535.0
            g[r, c] = word(u - 0x8000), word(v - 0x8000), word(trunc(Lf * Br))
        r1, r2, r3 = f32t(r1 + 0.02), f32t(r2 + 0.024), f32t(r3 + 0.016)
    return g


def grid_part4(T4, a1, a2, a3, B):
    P0, P1, P2 = (f32t(100.0 * math.sin(T4 * k)) for k in (0.03, 0.043, 0.053))
    g = np.zeros((21, 41, 3), np.int64)
    for r in range(21):
        for c in range(41):
            xs, ys = xy(c, r)
            x2, y3, z3 = rotate(xs, ys, a1, a2, a3)
            n = math.sqrt(x2 * x2 + y3 * y3 + z3 * z3)
            dx, dy, dz = f32t(x2 * (1 / n)), f32t(y3 * (1 / n)), f32t(1 / n * z3)
            t = max(cylinder(dx, dy, P0, P1), cylinder(dz, dy, P2, P1), cylinder(dz, dx, P2, P0))
            hx, hy, hz = dx * t + P0, dy * t + P1, f32t(t * dz + P2)
            u = trunc((hx + hy + hz) * 8.0) << 2
            v = trunc(8.0 * (hx + hy - hz)) << 2
            L = f32t(46534.0 - t * 878.0 * 0.026 * 0.00390625 * 192.0)
            if not L >= 14048.0: L = 14048.0
            elif L > 64535.0: L = 64535.0
            g[r, c] = word(u - 0x8000), word(v - 0x8000), word(trunc(L * B))
    return g


def compare(model, grid):
    ref = np.frombuffer(grid, '<u2').reshape(21, 41, 3).astype(np.int64)
    d = (model - ref) & 0xffff
    d = d - ((d & 0x8000) << 1)
    return int((d == 0).all(axis=2).sum()), np.abs(d).max(axis=(0, 1))


def check_state(part, e, f):
    """the recurrence: state at the filler call from the state at the part entry and dt (all float32)."""
    dt = F(e, 0x420489)
    errs = []
    if part == 5:
        errs.append(F(f, 0xea6be4) - f32t(dt * 0.005 + F(e, 0xea6be4)))
        errs.append(F(f, 0xea6be8) - f32t(dt * 0.005 * 2.0 * F(e, 0x421a54) + F(e, 0xea6be8)))
    if part == 2:
        errs.append(F(f, 0xf73928) - f32t(dt + F(e, 0xf73928)))
        br = F(e, 0x428160)
        if br < 1.0:
            br = f32t(dt * 0.01 + br)
            if br > 1.0: br = 1.0
        errs.append(F(f, 0x428160) - br)
    if part == 4:
        k = I(e, 0x421a7c)
        rates = [struct.unpack('<f', bytes(pickle_img[0x421a58 + 12 * k + 4 * i - 0x400000:][:4]))[0]
                 for i in range(3)] if pickle_img else None
        errs.append(F(f, 0x421a8c) - f32t(dt + F(e, 0x421a8c)))
        T4o = F(e, 0x421a8c)
        if I(e, 0x421a84) == 0:
            errs.append(F(f, 0x421a88) - f32t(math.sin(T4o * 0.07) * 0.3 + 0.5))
        if rates:
            for i, adr in enumerate((0x421a90, 0x421a94, 0x421a98)):
                errs.append(F(f, adr) - f32t(dt * rates[i] + F(e, adr)))
    return max((abs(x) for x in errs), default=0.0)


pickle_img = None


def main():
    global pickle_img
    pickle_img = open(os.path.join(HERE, '..', '..', 'unpacked', 'clone_img.bin'), 'rb').read()
    args = sys.argv[1:] or ['2', '4', '5']
    for arg in args:
        part, _, path = arg.partition(':')
        part = int(part)
        path = path or os.path.join(DUMP, 'p%d.pkl' % part)
        recs = [r for r in pickle.load(open(path, 'rb')) if r['part'] == part and 'grid' in r]
        tot = exact = 0
        worst = np.zeros(3, np.int64)
        st_err = 0.0
        for rec in recs:
            e, f = rec['entry'], rec['fill']
            st_err = max(st_err, check_state(part, e, f))
            if part == 5:
                m = grid_part5(F(f, 0xea6be4), F(f, 0xea6be8))
            elif part == 2:
                m = grid_part2(F(e, 0xf73928), F(f, 0xf73928), F(f, 0x428160), I(f, 0x42815c))
            else:
                m = grid_part4(F(f, 0x421a8c), F(f, 0x421a90), F(f, 0x421a94), F(f, 0x421a98), F(f, 0x421a88))
            n, w = compare(m, rec['grid'])
            exact += n
            tot += 41 * 21
            worst = np.maximum(worst, w)
        print('part %d: %d frames, state recurrence max error %.3g; grid entries exact %d of %d (%.3f %%), '
              'max |diff| u %d v %d light %d' % (part, len(recs), st_err, exact, tot, 100.0 * exact / tot, *worst))


if __name__ == '__main__':
    main()
