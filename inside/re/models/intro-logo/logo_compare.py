"""How much the logo's 3D frame changes when the shortcuts are evaluated exactly: perspective-correct u, v and
light instead of the engine's affine (screen-linear) interpolation, and a depth test instead of painter's order.
Writes side-by-side PNGs (capture | model affine | model perspective | model perspective + z-buffer)."""
import os, sys, pickle, struct
import numpy as np
from PIL import Image
import logo_model as L


def fill_persp(img, zb, a, b, c, cx=0.5, cy=0.5):
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
    iz = w0 / a[5] + w1 / b[5] + w2 / c[5]
    att = lambda j: (w0 * a[j] / a[5] + w1 * b[j] / b[5] + w2 * c[j] / c[5]) / iz
    u, v, lv = att(2), att(3), att(4)
    z = 1 / iz
    tex = L.TEX[np.floor(v).astype(np.int64) & 255, np.floor(u).astype(np.int64) & 255]
    lev = np.clip(np.floor(lv).astype(np.int64), 0, 63)
    sub = img[y0:y1 + 1, x0:x1 + 1]
    if zb is not None:
        zs = zb[y0:y1 + 1, x0:x1 + 1]
        m &= z < zs
        zs[m] = z[m]
    sub[m] = L.SHD[lev, tex][m]


def raster_persp(order, p, zbuf):
    img = np.zeros((200, 320), np.uint8)
    zb = np.full((200, 320), np.inf) if zbuf else None
    for f in order:
        tri = [(p[k, 0], p[k, 1], p[k, 2], (L.UW[k] + 1) / 16.0, (L.VW[k] + 1) / 16.0, (L.LIGHT[k] + 1) / 16.0) for k in L.FI[f]]
        poly = L.clip_near(tri) if min(t[2] for t in tri) < 96 else tri
        if len(poly) < 3:
            continue
        scr = [(160 + 42 * t[0] / t[2], 100 + 35 * t[1] / t[2], t[3], t[4], t[5], t[2]) for t in poly]
        for i in range(1, len(scr) - 1):
            fill_persp(img, zb, scr[0], scr[i], scr[i + 1])
    return img


ev = pickle.load(open(sys.argv[1], 'rb'))
out = sys.argv[2]
draws, t3d = {}, {}
for e in ev:
    if e['kind'] == 'f_draw' and e['n'] not in draws and e['refresh'] <= 2147: draws[e['n']] = e
    elif e['kind'] == 't_3d': t3d[e['n']] = e
pal = np.frombuffer(L.DAT.get('logo/logo.pal'), np.uint8).reshape(256, 3).astype(np.uint32)
lut = ((pal * 255 + 31) // 63).astype(np.uint8)
for n in [int(a) for a in sys.argv[3:]]:
    e = draws[n]; o, base = e['obj'], e['obj_addr']
    M = np.array(struct.unpack_from('<9d', o, 0x28)).reshape(3, 3)
    p, zc, sx, sy, code = L.transform(M)
    vis, key = L.visible_and_keys(zc, code)
    order = L.radix_order(vis, key)
    got = np.frombuffer(t3d[n]['target'], np.uint8).reshape(200, 320)
    aff = L.raster(order, p, None)
    per = raster_persp(order, p, False)
    perz = raster_persp(order, p, True)
    # also: every face with a depth test (no culling of faces off-screen matters only for speed)
    print('frame %d: affine model = capture %.3f; perspective vs affine differ %.3f; z-buffer vs painter differ %.4f' % (
        n, (aff == got).mean(), (per != aff).mean(), (perz != per).mean()))
    row = np.concatenate([lut[got], lut[aff], lut[per], lut[perz]], 1)
    Image.fromarray(row).resize((1280 * 2 // 2 * 1, 200 * 2 // 2 * 1), Image.NEAREST).save(os.path.join(out, 'logo_cmp_%d.png' % n))
