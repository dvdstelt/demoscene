"""A simple model of one drawn frame of the flower part, from the engine's per-vertex results that run_flower.py
dumped (camera-space coordinates, texture coordinates, light levels, face flags), compared with the capture.

What it models (the part's view of the engine): clear to index 0; every face that the engine marked visible (face
+0x30 bit 1) and that faces the camera on screen; painter's order by the engine's key (max camera z of the three
vertices minus the object's bias +0xcc, larger first); near clipping at camera z = 64 (z128 = 0x2000); projection
x = 160 + 256 X/Z, y = 100 + 213.33 Y/Z; affine texture coordinates and light (as the engine draws faces without subdivision bits; 1/16 texel units, 256x256 wrap,
column = u, row = v) and light; tunnel pixel = jeesus.shd[(light >> 4) * 256 + jeesus2[v][u]], flower pixel =
jeesus1[v][u].

usage: python3 models/flower/render.py models/flower/run/part.dump [count] [--png]
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import dump  # noqa: E402
import sro  # noqa: E402
from sheet import open_frames, indexed  # noqa: E402

d = sro.dat()
T1 = np.frombuffer(d.get('flower/jeesus1.raw'), np.uint8).reshape(256, 256)
T2 = np.frombuffer(d.get('flower/jeesus2.raw'), np.uint8).reshape(256, 256)
SHD = np.frombuffer(d.get('flower/jeesus.shd'), np.uint8).reshape(64, 256)
PAL = np.frombuffer(d.get('flower/jeesus.pal'), np.uint8).reshape(256, 3)
NEAR = 64.0
PERSPECTIVE = False
BIAS = 1.0
YS = 27306.0 / 32768.0


def object_faces(objb, V, F, cc, kind, light_shift, uv0=None):
    """triangles in camera space with attributes [x, y, z, u, v, light] per corner, plus key. For the tunnel
    (uv0 = (u0, v0)) the corners get the material's coordinates (0x41be3c): even faces (tunnel-mat1) (u0, v0),
    (u0 + 0x1000, v0), (u0 + 0x1000, v0 + 0x1000); odd faces (tunnel-mat2) (u0, v0), (u0 + 0x1000, v0 + 0x1000),
    (u0, v0 + 0x1000)."""
    base = struct.unpack_from('<I', objb, 0xd0)[0]
    out = []
    for fi, f in enumerate(F):
        fl = int(f[12]) & 0xffffffff
        if not fl & 2 or fl & 1:
            continue
        idx = [(int(f[5 + j]) & 0xffffffff) - base for j in range(3)]
        idx = [i // 64 for i in idx]
        corners = []
        for i in idx:
            v = V[i]
            x, y, z = v[8] / 128.0, v[9] / 128.0 / YS, v[7] / 128.0
            u = np.int16(v[10] & 0xffff)
            vv = np.int16((v[10] >> 16) & 0xffff)
            lt = np.int16(v[12] & 0xffff)
            corners.append([x, y, z, float(u), float(vv), float(lt)])
        if uv0 is not None:
            u0, v0 = uv0
            mc = [(u0, v0), (u0 + 0x1000, v0), (u0 + 0x1000, v0 + 0x1000)] if fi % 2 == 0 else \
                [(u0, v0), (u0 + 0x1000, v0 + 0x1000), (u0, v0 + 0x1000)]
            for j in range(3):
                corners[j][3], corners[j][4] = float(np.int16(mc[j][0] & 0xffff)), float(np.int16(mc[j][1] & 0xffff))
        zs = [int(V[i][7]) for i in idx]
        key = max(0, int((max(zs) - cc) / 16))
        out.append((key, kind, np.array(corners), fl))
    return out


def clip_near(poly):
    res = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        ina, inb = a[2] >= NEAR, b[2] >= NEAR
        if ina:
            res.append(a)
        if ina != inb:
            t = (NEAR - a[2]) / (b[2] - a[2])
            res.append(a + (b - a) * t)
    return res


def raster(img, poly, kind, tex_u_from_face=None):
    if len(poly) < 3:
        return 0
    P = np.array(poly)
    sx = 160 + 256 * P[:, 0] / P[:, 2]
    sy = 100 + 256 * YS * P[:, 1] / P[:, 2]
    area = 0.0
    for i in range(len(P)):
        j = (i + 1) % len(P)
        area += sx[i] * sy[j] - sx[j] * sy[i]
    if area >= 0:       # the engine skips faces with (y2-y0)(x1-x0) - (y1-y0)(x2-x0) > 0
        return 0
    drawn = 0
    for t in range(1, len(P) - 1):
        ids = [0, t, t + 1]
        x0, x1, x2 = sx[ids]
        y0, y1, y2 = sy[ids]
        minx, maxx = max(0, int(np.floor(min(x0, x1, x2)))), min(319, int(np.ceil(max(x0, x1, x2))))
        miny, maxy = max(0, int(np.floor(min(y0, y1, y2)))), min(199, int(np.ceil(max(y0, y1, y2))))
        if minx > maxx or miny > maxy:
            continue
        xs, ys = np.meshgrid(np.arange(minx, maxx + 1) + 0.0, np.arange(miny, maxy + 1) + 0.0)
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        l0 = ((y1 - y2) * (xs - x2) + (x2 - x1) * (ys - y2)) / den
        l1 = ((y2 - y0) * (xs - x2) + (x0 - x2) * (ys - y2)) / den
        l2 = 1 - l0 - l1
        m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        if not m.any():
            continue
        if PERSPECTIVE:
            iz = 1 / P[ids, 2]
            w = l0 * iz[0] + l1 * iz[1] + l2 * iz[2]
            att = [(l0 * P[ids[0], k] * iz[0] + l1 * P[ids[1], k] * iz[1] + l2 * P[ids[2], k] * iz[2]) / w
                   for k in (3, 4, 5)]
        else:   # the engine's path for faces without subdivision bits (flags >> 10 & 7 = 0): affine, +1 bias
            att = [l0 * P[ids[0], k] + l1 * P[ids[1], k] + l2 * P[ids[2], k] + BIAS for k in (3, 4, 5)]
        u = (np.floor(att[0] / 16).astype(np.int64)) & 255
        v = (np.floor(att[1] / 16).astype(np.int64)) & 255
        if kind == 'tunnel':
            lev = np.clip(np.floor(att[2] / 16).astype(np.int64), 0, 63)
            pix = SHD[lev, T2[v, u]]
        else:
            pix = T1[v, u]
        sub = img[miny:maxy + 1, minx:maxx + 1]
        sub[m] = pix[m]
        drawn += m.sum()
    return drawn


def render(pl, uv0):
    o = 0
    tun = pl[o:o + 0x110]; o += 0x110
    V = dump.verts(pl[o:], 732); o += 64 * 732
    F = dump.faces(pl[o:], 1440); o += 52 * 1440
    flb = pl[o:o + 0x110]; o += 0x110
    FV = dump.verts(pl[o:], 111); o += 64 * 111
    FF = dump.faces(pl[o:], 125); o += 52 * 125
    flags = struct.unpack_from('<I', flb, 0xc4)[0]
    tris = object_faces(tun, V, F, 0, 'tunnel', 4, uv0)
    if not flags & 1:
        tris += object_faces(flb, FV, FF, 0x35200, 'flower', 0)
    tris.sort(key=lambda t: -t[0])
    img = np.zeros((200, 320), np.uint8)
    for key, kind, C, fl in tris:
        raster(img, clip_near(list(C)), kind)
    return img


def main():
    path = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].isdigit() else 6
    png = '--png' in sys.argv
    raw, n = open_frames(os.path.join(HERE, '..', '..', 'full.frames'))
    done = 0
    tot_eq = tot = 0
    uv0 = None
    for tag, frame, pl in dump.records(path):
        if tag == 'MRPH':
            g = dump.glob(pl)
            uv0 = (g['u0'], g['v0'])
        if tag != 'DRWN' or uv0 is None:
            continue
        done += 1
        img = render(pl, uv0)
        best = None
        for k in (frame, frame + 1):
            cap, pal = indexed(raw, k)
            eq = np.count_nonzero(cap == img)
            if best is None or eq > best[0]:
                best = (eq, k, cap, pal)
        eq, k, cap, pal = best
        tot_eq += eq; tot += 64000
        print('drawn at refresh %d, compared with capture record %d: %d of 64000 pixels equal (%.1f%%)' % (
            frame, k, eq, eq / 640.0))
        if png:
            from PIL import Image
            lut = (PAL.astype(np.uint32) * 255 + 31) // 63
            diff = (cap != img)
            out = np.concatenate([lut[cap], lut[img], np.repeat((diff * 255)[:, :, None], 3, 2)], 1).astype(np.uint8)
            Image.fromarray(out).resize((960 * 2, 400), Image.NEAREST).save(os.path.join(HERE, 'render_%d.png' % frame))
        if done > count:
            break
    print('total %.1f%% equal' % (100.0 * tot_eq / max(1, tot)))


if __name__ == '__main__':
    main()
