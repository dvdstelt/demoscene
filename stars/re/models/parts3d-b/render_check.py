"""Approximate rasteriser for the engine modes used by parts3d-b, fed with the engine's own per-vertex results from a
dump (run_stars_b.py --dump-at, taken right after the draw call), to check the per-pixel formulas:

  mode 15 (part 0xa0ca):   pixel = tex[v][u] + light[(ev + tex[v][u+1]) & 255][(eu + tex[v][u]) & 255]
  modes 2 / 3 (env only):  pixel = env[ev][eu]
  mode 20 material 0:      pixel = light[(ev + bump[v][2u+1]) & 255][(eu + bump[v][2u]) & 255]   (u doubled, mod 256)
  mode 20 material 1:      pixel = env[ev][eu]

usage: render_check.py DUMP MODE [OBJ]   (MODE 15 / 3 / 20; OBJ = object struct address, default the part's)
Prints the share of pixels inside the drawn faces that the formula reproduces exactly.
"""
import pickle
import struct
import sys

import numpy as np

import snap
import os
OX = float(os.environ.get("OX", 0)); OY = float(os.environ.get("OY", 0))


def faces_of(m):
    nf = m.u32(0x55928)
    lst = m.u32(0x558f0)
    fbase = m.u32(0x55938)
    out = []
    for i in range(nf):
        e = m.u32(lst + 4 * i)
        fi = e >> 16
        rec = fbase + fi * 16
        out.append((m.u16(rec), m.u16(rec + 2), m.u16(rec + 4), m.u16(rec + 6)))
    return out


def raster(scr, attrs, faces, shade):
    """Fill the faces in list order; attrs: per vertex (u, v, eu, ev) as floats. shade(mat, U, V, EU, EV) -> bytes."""
    out = np.full((200, 320), -1, np.int32)
    ys, xs = np.mgrid[0:200, 0:320]
    for mat, a, b, c in faces:
        P = np.array([scr[a], scr[b], scr[c]], float)
        x0, x1 = int(max(0, np.floor(P[:, 0].min()))), int(min(319, np.ceil(P[:, 0].max())))
        y0, y1 = int(max(0, np.floor(P[:, 1].min()))), int(min(199, np.ceil(P[:, 1].max())))
        if x1 < x0 or y1 < y0: continue
        X = xs[y0:y1 + 1, x0:x1 + 1] + OX; Y = ys[y0:y1 + 1, x0:x1 + 1] + OY
        d = (P[1, 0] - P[0, 0]) * (P[2, 1] - P[0, 1]) - (P[2, 0] - P[0, 0]) * (P[1, 1] - P[0, 1])
        if abs(d) < 1e-9: continue
        w1 = ((X - P[0, 0]) * (P[2, 1] - P[0, 1]) - (P[2, 0] - P[0, 0]) * (Y - P[0, 1])) / d
        w2 = ((P[1, 0] - P[0, 0]) * (Y - P[0, 1]) - (X - P[0, 0]) * (P[1, 1] - P[0, 1])) / d
        w0 = 1 - w1 - w2
        inside = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
        if not inside.any(): continue
        A = np.array([attrs[a], attrs[b], attrs[c]], float)
        vals = [w0 * A[0, k] + w1 * A[1, k] + w2 * A[2, k] for k in range(4)]
        pix = shade(mat, *[np.floor(v).astype(np.int64) & 255 for v in vals])
        sub = out[y0:y1 + 1, x0:x1 + 1]
        sub[inside] = pix[inside]
    return out


def main():
    d = pickle.load(open(sys.argv[1], 'rb'))
    m = snap.Mem(d['mem'])
    mode = int(sys.argv[2])
    nv = m.u32(0x5592c)
    scrb, ecb = m.u32(0x558e4), m.u32(0x55908)
    scr = [(m.s16(scrb + 4 * i), m.s16(scrb + 4 * i + 2)) for i in range(nv)]
    faces = faces_of(m)
    if mode in (15, 20):
        attrs = []
        for i in range(nv):
            w = m.u32(ecb + 4 * i)
            attrs.append((w & 255, w >> 8 & 255, w >> 16 & 255, w >> 24 & 255))
    else:
        attrs = [(0, 0, m.u8(ecb + 2 * i), m.u8(ecb + 2 * i + 1)) for i in range(nv)]
    light = m.arr(0x582d0, 65536).reshape(256, 256)
    variant = sys.argv[4] if len(sys.argv) > 4 else ''
    if mode == 15:
        tp = m.u32(m.u32(0x582a6))                       # texture struct (0xa04e): pointer
        tex = m.arr(tp, 65536 + 256)
        def shade(mat, U, V, EU, EV):
            o = V * 256 + U
            t0, t1 = tex[o], tex[o + 1]
            if variant == 'nobump': t0b, t1b = 0 * t0, 0 * t1
            else: t0b, t1b = t0, t1
            return t0.astype(np.int32) + light[(EV + t1b) & 255, (EU + t0b) & 255]
    elif mode == 3:
        env = m.arr(m.u32(0x5590e), 65536).reshape(256, 256)
        def shade(mat, U, V, EU, EV): return env[EV, EU].astype(np.int32)
    elif mode == 20:
        bump = m.arr(m.u32(0x16d72), 65536 + 2)
        env = m.arr(m.u32(0x5590e), 65536).reshape(256, 256)
        def shade(mat, U, V, EU, EV):
            if mat == 1: return env[EV, EU].astype(np.int32)
            o = V * 256 + ((2 * U) & 255)
            return light[(EV + bump[o + 1]) & 255, (EU + bump[o]) & 255].astype(np.int32)
    out = raster(scr, attrs, faces, shade)
    screen = m.arr(m.u32(0x558cc), 64000).reshape(200, 320).astype(np.int32)
    drawn = out >= 0
    eq = (out == screen) & drawn
    print('%s mode %d: %d faces, %d px inside, exact %.1f %%, within +-1 index %.1f %%' % (
        sys.argv[1], mode, len(faces), drawn.sum(), 100 * eq.sum() / max(1, drawn.sum()),
        100 * ((np.abs(out - screen) <= 1) & drawn).sum() / max(1, drawn.sum())))
    if mode == 15:
        print('   texel part (pixel & 7) exact %.1f %%, light part (pixel >> 3) exact %.1f %%' % (
            100 * ((out & 7) == (screen & 7))[drawn].mean(), 100 * ((out >> 3) == (screen >> 3))[drawn].mean()))
    if len(sys.argv) > 3 and sys.argv[3] != '-':
        from PIL import Image
        pal = np.frombuffer(d['pal'], np.uint8).reshape(256, 3).astype(int)
        if pal.max() < 20: pal = np.arange(256)[:, None].repeat(3, 1) * 63 // 255     # (black palette: show indices)
        pal = (pal * 255 // 63).astype(np.uint8)
        a = np.where(drawn, out, screen)
        Image.fromarray(np.hstack([pal[screen.astype(np.uint8)], pal[a.astype(np.uint8)]])).save(sys.argv[3])


if __name__ == '__main__':
    main()
