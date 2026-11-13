"""The per-pixel functions of the grid parts: what the remake evaluates at every pixel instead of the 8x8 blocks.

Coordinates: (x, y) in the original 320 x 200 frame (pixel centres at +0.5 for a rendering; the original's grid
entry (c, r) sits at x = 8c, y = 20 + 8r). The functions return u, v in texels (unwrapped floats; the texture
repeats every 256) and the light L in table steps (the original's light word / 256). shade() turns them into
colours with the original's texture (the remake would evaluate the generated texture itself) and the exp decode.

usage: python3 models/grid/perpixel.py   (from clone/re)
  For dumped frames of parts 2, 4, 5: renders the per-pixel version at 320 x 200 and compares it with the
  original's frame (rows 20..179), and reports how far apart neighbouring grid entries are (|du|, |dv| > 128
  texels between neighbours = the original's 8x8 interpolation runs the wrong way round the texture).
  Writes side-by-side images to dump/grid/perpixel_<part>_<tick>.png.
"""
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import filler  # noqa: E402

RE = os.path.normpath(os.path.join(HERE, '..', '..'))
DUMP = os.path.join(RE, 'dump', 'grid')


def screen_dir(x, y):
    return (x - 160.0) / 120.0, (y - 100.0) / 120.0


def rotate(xs, ys, a1, a2, a3):
    x1 = np.sin(a3) * ys + np.cos(a3) * xs
    y1 = np.cos(a3) * ys - np.sin(a3) * xs
    z2 = np.sin(a2) * x1 + np.cos(a2)
    x2 = np.cos(a2) * x1 - np.sin(a2)
    y3 = np.sin(a1) * z2 + np.cos(a1) * y1
    z3 = np.cos(a1) * z2 - np.sin(a1) * y1
    return x2, y3, z3


def cylinder(da, db, pa, pb, R=192.0):
    a = da * da + db * db
    b = 2.0 * (pa * da + pb * db)
    c = pa * pa + pb * pb - R * R
    s = np.sqrt(np.maximum(b * b - 4.0 * a * c, 0.0))
    t1 = (s - b) / (2.0 * a + 0.001)
    t2 = (-b - s) / (2.0 * a + 0.001)
    return np.where(t1 > 0, t1, t2)


def part5(x, y, T, R):
    c, r = x / 8.0, (y - 20.0) / 8.0
    X, Y = (x - 160.0) * 67200.0, (y - 100.0) * 67200.0          # (c - 20) * 0x83400, (r - 10) * 0x83400
    Xr = np.cos(R) * X - np.sin(R) * Y
    Yr = np.sin(R) * X + np.cos(R) * Y
    S = 130.0 * np.sin(0.141 * c + 4.0 * T) * np.sin(0.296 * r + 7.0 * T) + \
        55.0 * np.sin(0.261 * c + 6.0 * T) * np.sin(0.176 * r + 8.0 * T)
    Z = 200.0 + S * np.sin(5.0 * T)
    return (Xr / Z + 32768.0) / 256.0, (Yr / Z + 32768.0) / 256.0, (18432.0 + 81.92 * Z) / 256.0


def part2(x, y, TCo, TCn, Br, flag):
    if flag:
        Ox, Oz = 110.0 * np.sin(0.013 * TCo + 2.0), 3.0 * TCo
    else:
        Ox, Oz = 150.0 * np.sin(0.01 * TCo), 5.0 * TCo
    row = (y - 20.0) / 8.0
    r1, r2, r3 = 0.01 * TCn + 0.02 * row, 0.012 * TCn + 0.024 * row, 0.008 * TCn + 0.016 * row
    xs, ys = screen_dir(x, y)
    x2, y3, z3 = rotate(xs, ys, r1, r2, r3)
    n = np.sqrt(x2 * x2 + y3 * y3 + z3 * z3)
    dx, dy, dz = 0.17 * x2 / n, y3 / n, z3 / n
    t = cylinder(dx, dy, Ox, 0.0)
    hx, hy, hz = Ox + t * dx, t * dy, Oz + t * dz
    u = 19.2 * hz / 256.0 + 128.0
    v = np.arctan2(hx, hy) / np.pi * 256.0 + 128.0
    L = Br * 878.0 * np.minimum(np.maximum(53.0 - 0.018 * t, 16.0), 64535.0 / 878.0) / 256.0
    return u, v, L


def part4(x, y, T4, a1, a2, a3, B):
    P0, P1, P2 = 100.0 * np.sin(0.03 * T4), 100.0 * np.sin(0.043 * T4), 100.0 * np.sin(0.053 * T4)
    xs, ys = screen_dir(x, y)
    x2, y3, z3 = rotate(xs, ys, a1, a2, a3)
    n = np.sqrt(x2 * x2 + y3 * y3 + z3 * z3)
    dx, dy, dz = x2 / n, y3 / n, z3 / n
    t = np.maximum(np.maximum(cylinder(dx, dy, P0, P1), cylinder(dz, dy, P2, P1)), cylinder(dz, dx, P2, P0))
    hx, hy, hz = P0 + t * dx, P1 + t * dy, P2 + t * dz
    u = 32.0 * (hx + hy + hz) / 256.0 + 128.0
    v = 32.0 * (hx + hy - hz) / 256.0 + 128.0
    L = B * np.clip(46534.0 - 17.121 * t, 14048.0, 64535.0) / 256.0
    return u, v, L


def shade(u, v, L, tex):
    """bilinear in the texture's (log-encoded) bytes, exact wrap, then out = min(255, e^((L + c - 256) / 23))."""
    t = tex[:65536].reshape(256, 256).astype(np.float64)
    chans = [(t.astype(np.int64) >> s) & 255 for s in (16, 8, 0)]
    u0, v0 = np.floor(u), np.floor(v)
    fu, fv = u - u0, v - v0
    iu, iv = u0.astype(np.int64) & 255, v0.astype(np.int64) & 255
    iu1, iv1 = (iu + 1) & 255, (iv + 1) & 255
    out = []
    for ch in chans:
        c = ch[iu, iv] * (1 - fu) * (1 - fv) + ch[iu, iv1] * (1 - fu) * fv + ch[iu1, iv] * fu * (1 - fv) + \
            ch[iu1, iv1] * fu * fv
        out.append(np.minimum(255.0, np.exp((L + c - 256.0) / 23.0)))
    return np.stack(out, axis=-1)


def F(d, a): return struct.unpack('<f', d[a][:4])[0]


def I(d, a): return struct.unpack('<i', d[a][:4])[0]


def state_fn(part, rec):
    e, f = rec['entry'], rec['fill']
    if part == 5:
        return lambda x, y: part5(x, y, F(f, 0xea6be4), F(f, 0xea6be8))
    if part == 2:
        return lambda x, y: part2(x, y, F(e, 0xf73928), F(f, 0xf73928), F(f, 0x428160), I(f, 0x42815c))
    return lambda x, y: part4(x, y, F(f, 0x421a8c), F(f, 0x421a90), F(f, 0x421a94), F(f, 0x421a98), F(f, 0x421a88))


def main():
    from PIL import Image
    snaps = {2: 't2316.snap', 5: 't6156.snap', 4: 't9324.snap'}
    for part in (2, 5, 4):
        T, W, tex = filler.tables(os.path.join(RE, 'snaps', snaps[part]))
        recs = [r for r in pickle.load(open(os.path.join(DUMP, 'p%d.pkl' % part), 'rb')) if r['part'] == part]
        gy, gx = np.mgrid[0:21, 0:41]
        worst_du = []
        diffs = []
        for k, rec in enumerate(recs):
            fn = state_fn(part, rec)
            u, v, L = fn(gx * 8.0, 20.0 + gy * 8.0)
            du = max(np.abs(np.diff(u, axis=1)).max(), np.abs(np.diff(u, axis=0)).max())
            dv = max(np.abs(np.diff(v, axis=1)).max(), np.abs(np.diff(v, axis=0)).max())
            worst_du.append(max(du, dv))
            # per-pixel render at the original's pixel positions (corners, as the filler samples them)
            py, px = np.mgrid[20:180, 0:320].astype(np.float64)
            u, v, L = fn(px, py)
            img = shade(u, v, L, tex)
            ref = filler.page_rows(rec['filled'])
            refc = np.stack([(ref >> 16) & 255, (ref >> 8) & 255, ref & 255], axis=-1).astype(np.float64)
            diffs.append(np.abs(img - refc).mean())
            if k in (len(recs) // 3, 2 * len(recs) // 3):
                both = np.concatenate([refc, img], axis=1).clip(0, 255).astype(np.uint8)
                Image.fromarray(both).save(os.path.join(DUMP, 'perpixel_%d_%d.png' % (part, rec['tick'])))
        wd = np.array(worst_du)
        print('part %d: %d frames; per-pixel vs original (0..255): median %.2f, max %.2f; frames whose grid has '
              'neighbours more than 128 texels apart: %d (max %.0f texels)' % (
                  part, len(recs), np.median(diffs), max(diffs), int((wd > 128).sum()), wd.max()))


if __name__ == '__main__':
    main()
