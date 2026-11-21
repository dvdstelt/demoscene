"""Render the landscape of a dumped frame with a per-pixel model (perspective-correct texture coordinates,
screen-linear light and fog) and compare it with the frame the original drew (AFTR record of run1.dump).

usage: python3 landrender.py [record index ...]   writes cmp_<frame>.png (model | original | difference)

Model: world vertex = landscape object position + vertex (x, y, z); camera space v = C^T (world - camera), C the
camera matrix (columns = camera axes); screen x = 160 + 200 x / z, y = 100 + 166.667 y / z; texel (u / 8, v / 8)
of map.raw (512 x 512, wrapping); index = fog[fog >> 4][shd[light >> 4][texel]].
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile  # noqa: E402
import fdump  # noqa: E402

DAT = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
TEX = np.frombuffer(DAT.get('flight/map.raw'), np.uint8).reshape(512, 512)
SHD = np.frombuffer(DAT.get('flight/map.shd'), np.uint8).reshape(64, 256)
FOG = np.frombuffer(DAT.get('flight/map.fog'), np.uint8).reshape(128, 256)
PAL = np.frombuffer(DAT.get('flight/map.pal'), np.uint8).reshape(256, 3)
ENGINE = True                   # sample at (column + 1, row + 15/16) with the +1/16 bias (notes-engine.md)
AFFINE = True                   # the engine's path for faces with +0x30 = 0 (no subdivision): affine
LUT = ((PAL.astype(int) * 255 + 31) // 63).astype(np.uint8)


def render(r, off=0.0, uscale=1 / 8):
    land = r.data[fdump.LAND: fdump.LAND + 0xdeec]
    pos = np.array(struct.unpack_from('<3i', land, 0x10), float)
    camobj = r.data[fdump.SCENE + 0x50: fdump.SCENE + 0x50 + 0xc8]
    cam = np.array(struct.unpack_from('<3i', camobj, 0x1c), float)
    C = np.array(struct.unpack_from('<9d', camobj, 0x70)).reshape(3, 3)
    xyz, uv, lf = fdump.land_vertices(r)
    w = xyz.astype(float) + pos
    v = (w - cam) @ C                        # = C^T (w - cam) per row
    zb = np.full((200, 320), np.inf)
    img = np.full((200, 320), 0xdd, np.uint8)
    faces = np.frombuffer(land, dtype=np.uint8, count=648 * 0x34, offset=0x10c + 361 * 0x40).reshape(648, 0x34)
    base = struct.unpack_from('<I', land, 0xd0)[0]
    idx = (faces[:, 0x14:0x20].copy().view('<u4') - base) // 0x40
    ys, xs = np.mgrid[0:200, 0:320]
    px, py = (xs + 1.0, ys + 15 / 16) if ENGINE else (xs + 0.5 + off, ys + 0.5 + off)
    for f in idx:
        P = v[f]
        if (P[:, 2] < 32).any():
            continue
        sx = 160 + 200 * P[:, 0] / P[:, 2]
        sy = 100 + (21333 / 16384 * 128) * P[:, 1] / P[:, 2]
        x0, x1 = int(max(0, np.floor(sx.min()))), int(min(319, np.ceil(sx.max())))
        y0, y1 = int(max(0, np.floor(sy.min()))), int(min(199, np.ceil(sy.max())))
        if x0 > x1 or y0 > y1:
            continue
        X, Y = px[y0:y1 + 1, x0:x1 + 1], py[y0:y1 + 1, x0:x1 + 1]
        d = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0])
        if abs(d) < 1e-9:
            continue
        b1 = ((X - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (Y - sy[0])) / d
        b2 = ((sx[1] - sx[0]) * (Y - sy[0]) - (X - sx[0]) * (sy[1] - sy[0])) / d
        b0 = 1 - b1 - b2
        inside = (b0 >= 0) & (b1 >= 0) & (b2 >= 0)
        if not inside.any():
            continue
        iz = b0 / P[0, 2] + b1 / P[1, 2] + b2 / P[2, 2]
        z = 1 / iz
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        m = inside & (z < sub)
        if not m.any():
            continue
        if AFFINE:
            uu = b0 * uv[f[0], 0] + b1 * uv[f[1], 0] + b2 * uv[f[2], 0]
            vv = b0 * uv[f[0], 1] + b1 * uv[f[1], 1] + b2 * uv[f[2], 1]
        else:
            uu = (b0 * uv[f[0], 0] / P[0, 2] + b1 * uv[f[1], 0] / P[1, 2] + b2 * uv[f[2], 0] / P[2, 2]) * z
            vv = (b0 * uv[f[0], 1] / P[0, 2] + b1 * uv[f[1], 1] / P[1, 2] + b2 * uv[f[2], 1] / P[2, 2]) * z
        li = b0 * lf[f[0], 0] + b1 * lf[f[1], 0] + b2 * lf[f[2], 0]
        fo = b0 * lf[f[0], 1] + b1 * lf[f[1], 1] + b2 * lf[f[2], 1]
        bias = 0.5 if ENGINE else 0.0             # 1/16 texel = 0.5 u units; 1/16 level = 1 light / fog unit
        tu = np.floor((uu + bias) * uscale).astype(int) & 511
        tv = np.floor((vv + bias) * uscale).astype(int) & 511
        t = TEX[tv, tu]
        lb = 1 if ENGINE else 0
        c = FOG[np.clip(np.floor(fo + lb), 0, 1023).astype(int) >> 4,
                SHD[np.clip(np.floor(li + lb), 0, 1023).astype(int) >> 4, t]]
        sub[m] = z[m]
        img[y0:y1 + 1, x0:x1 + 1][m] = c[m]
    return img


if __name__ == '__main__':
    recs = fdump.read(os.path.join(HERE, 'run1.dump'))
    rend = [r for r in recs if r.kind == b'REND']
    aftr = [r for r in recs if r.kind == b'AFTR']
    sel = [int(a) for a in sys.argv[1:]] or [100, 600, 1200]
    for k in sel:
        orig = np.frombuffer(aftr[k].data[:64000], np.uint8).reshape(200, 320)
        model = render(rend[k])
        land = (orig != 0xdd)
        eq = (model == orig)
        print('record %d frame %d: %.1f %% of all pixels equal, %.1f %% of the original\'s non-sky pixels'
              % (k, rend[k].frame, 100 * eq.mean(), 100 * eq[land].mean()))
        diff = np.where(eq, 0, 255).astype(np.uint8)
        im = np.concatenate([LUT[model], LUT[orig], np.stack([diff] * 3, -1)], 1)
        Image.fromarray(im).resize((im.shape[1] * 2, im.shape[0] * 2), Image.NEAREST).save(
            os.path.join(HERE, 'cmp_%d.png' % rend[k].frame))
