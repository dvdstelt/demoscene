"""Model of the logo part's 3D frame (obuke.sro, twlogo.raw through logo.shd), checked against the dump of a run
resumed from snaps/f1446.snap with hooks_logo.py.

Geometry, light, culling and sort order are computed from the formulas in notes-intro-logo.md and compared with
the engine's records; then the frame is rasterised (affine u, v and light, painter's order) and compared with the
draw target after the engine (t_3d) pixel by pixel.

usage: python3 logo_model.py <run>.logo.pkl [draw indices...] [--png DIR] [--geom: geometry of every frame only]
"""
import os, sys, struct, pickle
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
from sro import load

DAT = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
TEX = np.frombuffer(DAT.get('logo/twlogo.raw'), np.uint8).reshape(256, 256)
SHD = np.frombuffer(DAT.get('logo/logo.shd'), np.uint8).reshape(64, 256)
SRO = load()
P0 = SRO['V'][:, :3].astype(np.float64)
UW = (SRO['V'][:, 3] >> 4).astype(np.int64)               # vertex +0x28: u >> 4 (0..4095)
VW = -(SRO['V'][:, 4] >> 4).astype(np.int64)              # vertex +0x2a: -(v >> 4)
FI = SRO['F'][:, 1:4]


def rot(a1, a2, a3):
    c, s = np.cos(a1), np.sin(a1); rx = np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    c, s = np.cos(a2), np.sin(a2); ry = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    c, s = np.cos(a3), np.sin(a3); rz = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    return rx @ ry @ rz


S = rot(-0.01, 0.0085, 0.012345)         # 0x4145f4(0x425018, -0.01, 0.0085, 0.012345)


def normals():
    fn = np.cross(P0[FI[:, 1]] - P0[FI[:, 0]], P0[FI[:, 2]] - P0[FI[:, 0]])
    fn = np.trunc(1024 * fn / np.linalg.norm(fn, axis=1)[:, None])
    acc = np.zeros((len(P0), 3))
    for f in range(len(FI)):
        for k in FI[f]:
            acc[k] += fn[f]
    return fn, np.trunc(819.2 * acc / np.linalg.norm(acc, axis=1)[:, None])


FN, VN = normals()


def vertex_light():
    """0x41b508: point light at the object's centre, intensity 1600, range 550; ambient 0; cap 992."""
    d = np.linalg.norm(P0, axis=1)
    dot = (-P0 * VN).sum(1)
    lit = np.trunc((1600 - d * (1600 / 550)) * (dot / (d * 512)))
    lit = np.where((dot >= 0) & (d <= 550), lit, 0)
    return np.minimum(lit, 992)


LIGHT = vertex_light()


def transform(M):
    """camera space (x, y, z) in world units, z_c (int, x128), screen (28.4), outcodes."""
    p = P0 @ M.T
    p[:, 2] -= 100.0                     # camera at (0, 0, 100), identity orientation
    zc = np.trunc(p[:, 2] * 128)
    sx = np.trunc(2560 + 16 * 42 * p[:, 0] / p[:, 2])
    sy = np.trunc(1600 + 16 * 35 * p[:, 1] / p[:, 2])
    behind = zc < 12288
    code = np.where(sx < 0, 0x10000, 0) | np.where(sx >= 5120, 0x20000, 0) | np.where(sy < 0, 0x40000, 0) | \
        np.where(sy >= 3200, 0x80000, 0)
    code = np.where(behind, 0x100000, code)
    return p, zc, sx, sy, code


def front_faces(M):
    """0x41a458: back-face test in object space: the camera (0, 0, 100) taken into object space by the transpose
    of the object's matrix; a face is drawn when (camera - first vertex) . face normal >= 0."""
    c = M.T @ np.array([0.0, 0.0, 100.0])
    return ((c - P0[FI[:, 0]]) * FN).sum(1) >= 0


def visible_and_keys(zc, code, front=None):
    c = code[FI]
    andc = c[:, 0] & c[:, 1] & c[:, 2]
    vis = andc == 0
    if front is not None:
        vis &= front
    key = np.trunc(zc[FI].sum(1) * (1 / 3))
    key = np.maximum(np.trunc(key / 16), 0).astype(np.int64)
    return vis, key


def radix_order(vis, key):
    """0x41a9d4 builds the list by prepending faces in object order; 0x40c384 sorts it (3 byte passes)."""
    lst = [f for f in range(len(FI)) if vis[f]][::-1]
    t1 = [[] for _ in range(256)]
    for f in lst:
        t1[(key[f] & 0xff) ^ 0xff].insert(0, f)
    t2 = [[] for _ in range(256)]
    for b in range(256):
        for f in t1[b]:
            t2[(key[f] >> 8) & 0xff].insert(0, f)
    t3 = [[] for _ in range(256)]
    for b in range(256):
        for f in t2[b]:
            t3[(key[f] >> 16) & 0xff].insert(0, f)
    out = []
    for b in range(255, -1, -1):
        out += t3[b]
    return out


def clip_near(tri):
    """tri: list of (x, y, z, u, v, l) camera-space vertices; clip against z = 96 (12288 / 128)."""
    zn = 96.0
    out = []
    for i in range(len(tri)):
        a, b = tri[i], tri[(i + 1) % len(tri)]
        ain, bin_ = a[2] >= zn, b[2] >= zn
        if ain:
            out.append(a)
        if ain != bin_:
            t = (zn - a[2]) / (b[2] - a[2])
            out.append(tuple(a[j] + t * (b[j] - a[j]) for j in range(6)))
    return out


def raster(order, p, frame, cx=0.5, cy=0.5, quant=False):
    img = np.zeros((200, 320), np.uint8)
    for f in order:
        tri = [(p[k, 0], p[k, 1], p[k, 2], (UW[k] + 1) / 16.0, (VW[k] + 1) / 16.0, (LIGHT[k] + 1) / 16.0) for k in FI[f]]
        poly = clip_near(tri) if min(t[2] for t in tri) < 96 else tri
        if len(poly) < 3:
            continue
        scr = [(160 + 42 * t[0] / t[2], 100 + 35 * t[1] / t[2], t[3], t[4], t[5]) for t in poly]
        if quant:                        # the engine's 28.4 screen coordinates (truncated)
            scr = [(np.trunc(16 * a[0]) / 16, np.trunc(16 * a[1]) / 16) + a[2:] for a in scr]
        for i in range(1, len(scr) - 1):
            fill(img, scr[0], scr[i], scr[i + 1], cx, cy)
    return img


def fill(img, a, b, c, cx, cy):
    xs = np.array([a[0], b[0], c[0]]); ys = np.array([a[1], b[1], c[1]])
    x0, x1 = max(int(np.floor(xs.min())), 0), min(int(np.ceil(xs.max())), 319)
    y0, y1 = max(int(np.floor(ys.min())), 0), min(int(np.ceil(ys.max())), 199)
    if x0 > x1 or y0 > y1:
        return
    X, Y = np.meshgrid(np.arange(x0, x1 + 1) + cx, np.arange(y0, y1 + 1) + cy)
    den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    if abs(den) < 1e-12:
        return
    w0 = ((b[1] - c[1]) * (X - c[0]) + (c[0] - b[0]) * (Y - c[1])) / den
    w1 = ((c[1] - a[1]) * (X - c[0]) + (a[0] - c[0]) * (Y - c[1])) / den
    w2 = 1 - w0 - w1
    m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
    if not m.any():
        return
    u = w0 * a[2] + w1 * b[2] + w2 * c[2]
    v = w0 * a[3] + w1 * b[3] + w2 * c[3]
    lv = w0 * a[4] + w1 * b[4] + w2 * c[4]
    tex = TEX[np.floor(v).astype(np.int64) & 255, np.floor(u).astype(np.int64) & 255]
    lev = np.clip(np.floor(lv).astype(np.int64), 0, 63)
    sub = img[y0:y1 + 1, x0:x1 + 1]
    sub[m] = SHD[lev, tex][m]


def main():
    args = [a for a in sys.argv[2:] if not a.startswith('--')]
    png = None
    if '--png' in sys.argv:
        png = sys.argv[sys.argv.index('--png') + 1]
        args = [a for a in args if a != png]
    ev = pickle.load(open(sys.argv[1], 'rb'))
    snapS = None
    ticks = 1                               # the snapshot f1446 already holds S^1 (one tick routine call)
    draws, t3d = {}, {}
    for e in ev:
        if e['kind'] == 'tick':
            ticks += 1
        elif e['kind'] == 'f_draw' and e['n'] not in draws and e['refresh'] <= 2147:
            e['ticks'] = ticks
            draws[e['n']] = e
        elif e['kind'] == 't_3d':
            t3d[e['n']] = e
    print('S from angles vs dumped first tick: S^2 err %.2e' % np.abs(np.linalg.matrix_power(S, 2) - np.array(
        next(e for e in ev if e['kind'] == 'tick')['mat']).reshape(3, 3)).max())
    sel = [int(a) for a in args] or (sorted(draws) if '--geom' in sys.argv else sorted(t3d))
    stats = []
    for n in sel:
        e = draws[n]
        o, base = e['obj'], e['obj_addr']
        M = np.array(struct.unpack_from('<9d', o, 0x28)).reshape(3, 3)
        # which power of S is it? (the copy 0x402e44 made before the draw; a tick may come in between)
        k = min(range(max(0, e['ticks'] - 3), e['ticks'] + 2), key=lambda k: np.abs(np.linalg.matrix_power(S, k) - M).max())
        Mk = np.linalg.matrix_power(S, k)
        V = np.array([struct.unpack_from('<16i', o, 0x10c + 0x40 * i) for i in range(130)])
        p, zc, sx, sy, code = transform(Mk)
        fl = V[:, 1] & 0x1f0000
        okv = (fl & 0x100000) == 0
        dz = np.abs(zc - V[:, 7]).max()
        dsx = np.abs(sx - V[:, 5])[okv].max(); dsy = np.abs(sy - V[:, 6])[okv].max()
        dcode = (code != fl).sum()
        Fr = np.array([struct.unpack_from('<13i', o, 0x10c + 130 * 0x40 + 0x34 * i) for i in range(256)])
        front = front_faces(Mk)
        dfront = (Fr[:, 12] & 2) != 0
        vis, key = visible_and_keys(V[:, 7], fl, dfront)
        order = radix_order(vis, key)
        dumped = [(a - (base + 0x10c + 130 * 0x40)) // 0x34 for a in e['order']]
        vis2, key2 = visible_and_keys(zc, code, front)
        order2 = radix_order(vis2, key2)
        msg = 'back faces %d (%s) | ' % ((~front).sum(), 'same' if (front == dfront).all() else 'DIFF')
        msg += 'draw %d refresh %d k=%d (ticks %d) |S^k-M| %.1e | z err %d, sx err %d, sy err %d, outcode diffs %d | ' \
              'order from dumped z: %s, from model: %s (%d faces)' % (
                  n, e['refresh'], k, e['ticks'], np.abs(Mk - M).max(), dz, dsx, dsy, dcode,
                  'same' if order == dumped else 'DIFF', 'same' if order2 == dumped else 'DIFF', len(dumped))
        if n in t3d and '--geom' not in sys.argv:
            got = np.frombuffer(t3d[n]['target'], np.uint8).reshape(200, 320)
            img = raster(dumped, p, None)
            same = (img == got).mean()
            cov_model, cov_got = (img != 0).mean(), (got != 0).mean()
            # level error where both draw the same texel class: compare brightness through the palette
            msg += ' | pixels equal %.4f (covered model %.3f, capture %.3f)' % (same, cov_model, cov_got)
            stats.append(same)
            if png:
                from PIL import Image
                pal = np.frombuffer(DAT.get('logo/logo.pal'), np.uint8).reshape(256, 3).astype(np.uint32)
                lut = ((pal * 255 + 31) // 63).astype(np.uint8)
                diff = np.where(img == got, 0, 255).astype(np.uint8)
                row = np.concatenate([lut[got], lut[img], np.stack([diff] * 3, -1)], 1)
                Image.fromarray(row).resize((960 * 2, 400), Image.NEAREST).save(os.path.join(png, 'logo_model_%d.png' % n))
        print(msg)
    if stats:
        print('mean pixel agreement %.4f over %d frames' % (np.mean(stats), len(stats)))


if __name__ == '__main__':
    main()
