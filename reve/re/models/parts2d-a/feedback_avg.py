"""Entries 12/13: continuous colour models of the feedback (notes 5.4) against the exact model and the capture.

usage: python3 feedback_avg.py single|classes [--bias 0.375] [--out cmp.png]

Runs spiral.py's exact model from entry 12's start next to a colour model at 320 x 200:
  single:  B <- 3/4 B + 1/4 T, then B <- 3/4 B + 1/4 B(zoom) - bias, then the dots
  classes: four fields, one per pixel class; class g mod 4 takes T, the others keep theirs; M = their mean;
           B_c <- 3/4 M_c + 1/4 M(zoom) - bias, then the dots; shown: the mean of the fields
(T = layer 1 per pixel, nearest; zoom = the mean source imm + frac + (x - 1/2) s; bias in 6-bit levels). Prints the
mean luminance of the capture, the exact model and the colour model at records 8600, 9100, 9500, 9700; --out writes
them side by side (capture, exact, model). Takes a few minutes.
"""
import argparse
import math

import numpy as np
from PIL import Image

import spiral
from common import raw, file, frames, rgb, pal6_to_rgb, Mem, snap

RECORDS = (8600, 9100, 9500, 9700)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('model', choices=('single', 'classes'))
    ap.add_argument('--bias', type=float, default=0.375)
    ap.add_argument('--out')
    a = ap.parse_args()
    bias = a.bias / 63
    tpx, tpal = raw('GFX\\TEXTURE.RAW')
    pal = (tpal >> 2).astype(float) / 63
    lut8 = pal6_to_rgb(tpal >> 2)
    tex16 = tpx[80:96, 16:32].reshape(-1)
    table = np.frombuffer(file('GFX\\SPIRBLUR.PAL'), np.uint8)
    spx, _ = raw('GFX\\SPIRALE.RAW')
    dot = spx[:6, :6]
    raw_frames, n = frames()
    m = Mem(snap('snaps/f7700.snap'))
    sx, sm = spiral.Spiral(None), spiral.Spiral(None)
    for s in (sx, sm):
        s.roto = spiral.RotoState(m)
        s.a, s.p = spiral.s32(m.u32(0x2174c8)), m.u32(0x2174cc)
    Y, X = np.mgrid[0:200, 0:320] + 0.5

    def T(ang):
        t = ang / 2 * 2 * math.pi / 4096
        u = 32767 / 2 ** 17 * (np.sin(t) * (X - 161) - np.cos(t) * (Y - 101))
        v = 32767 / 2 ** 17 * (np.cos(t) * (X - 161) + np.sin(t) * (Y - 101))
        return pal[tex16.reshape(16, 16)[np.floor(u).astype(int) % 16, np.floor(v).astype(int) % 16]]

    def zoomed(img, z):
        step = (z << 22) & 0xffffffff
        h, s = step >> 16, step / 2 ** 32
        ex, ey = 0x1400000 - 320 * h, 0xc80000 - 200 * h
        xs = (ex >> 17) + (ex >> 1) / 2 ** 32 + (X - 0.5) * s
        ys = (ey >> 17) + (ey >> 1) / 2 ** 32 + (Y - 0.5) * s
        return img[np.clip(np.floor(ys).astype(int), 0, 199), np.clip(np.floor(xs).astype(int), 0, 319)]

    def dots(img, q):
        for k in range(30):
            b = ((q + 300 * k) & 0xfff) * 2
            R = 50 + 5 * k
            y = 100 + ((spiral.sin_word(b) * R) >> 16)
            x = 160 + ((spiral.cos_word(b) * R) >> 16)
            for r in range(6):
                for c in range(6):
                    if y + r < 200 and dot[r, c]:
                        img[y + r, x + c] = pal[dot[r, c]]

    exact = np.zeros((200, 320), np.uint8)
    fields = [np.zeros((200, 320, 3)) for _ in range(4 if a.model == 'classes' else 1)]
    rows = []
    for g, k in enumerate(range(spiral.SET12 - 1, max(RECORDS))):
        if k == spiral.SET13:
            for s in (sx, sm):
                s.ptr, s.cnt = spiral.LIST13, 1
        if g:
            sx.retrace()
            sm.retrace()
        spiral.frame(exact, sx, tex16, table, dot)
        t = T(sm.a)
        if a.model == 'single':
            mm = [0.75 * fields[0] + 0.25 * t]
        else:
            mm = [t if g % 4 == c else fields[c] for c in range(4)]
        z = zoomed(sum(mm) / len(mm), sm.zoom)
        fields = [np.maximum(0.75 * mc + 0.25 * z - bias, 0) for mc in mm]
        for f in fields:
            dots(f, sm.qprev)
        r = k + 1
        if r in RECORDS:
            cap = rgb(raw_frames, r).astype(float)
            ex = lut8[exact].astype(float)
            model = sum(fields) / len(fields) * 255
            print('record %d mean luminance: capture %.1f, exact %.1f, %s %.1f' % (r, cap.mean(), ex.mean(), a.model,
                                                                              model.mean()))
            rows.append(np.concatenate([cap, ex, model], 1))
    if a.out:
        img = np.concatenate(rows, 0).clip(0, 255).astype(np.uint8)
        Image.fromarray(img).resize((img.shape[1] * 2, img.shape[0] * 2), Image.NEAREST).save(a.out)


if __name__ == '__main__':
    main()
