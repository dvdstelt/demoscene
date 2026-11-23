"""Entry 7: what the layer checker averages to, against the capture (notes 3.3).

usage: python3 wizmix.py [--record 3500] [--out cmp.png]

Evaluates both rotozoom layers with the continuous mapping (nearest texel reads) and compares, after a 4 x 4 box
filter (the checker averaged away), the capture with (a) the age-weighted mix the classes hold over two frames:
1/8 (L1(n) + L1(n-1)) + 1/4 (L2(n) + L2(n-1)) + 1/8 (L2(n-2) + L2(n-3)), and (b) a single-time 1/4 L1 + 3/4 L2.
"""
import argparse
import math

import numpy as np
from PIL import Image

import wizroto
from common import raw, frames, rgb, pal6_to_rgb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--record', type=int, default=3500)
    ap.add_argument('--out')
    a = ap.parse_args()
    tpx, tpal = raw('GFX\\TEXTURE.RAW')
    lut = pal6_to_rgb(tpal >> 2).astype(float)
    t16, t64 = tpx[96:112, 16:32], tpx[64:128, 64:128]
    Y, X = np.mgrid[0:200, 0:320] + 0.5
    dx, dy = X - 161, Y - 101

    def layer(t, scale, tex, size):
        u = scale * (np.sin(t) * dx - np.cos(t) * dy)
        v = scale * (np.cos(t) * dx + np.sin(t) * dy)
        return lut[tex[np.floor(u).astype(int) % size, np.floor(v).astype(int) % size]]

    w = wizroto.Wiz(None)
    A = [w.a]
    for _ in range(a.record - wizroto.START):
        w.retrace()
        A.append(w.a)
    L1 = lambda age: layer(A[-1 - age] / 2 * 2 * math.pi / 4096, 32767 / 2 ** 17, t16, 16)
    L2 = lambda age: layer(-A[-1 - age] / 2 * 2 * math.pi / 4096, 32767 / 2 ** 16, t64, 64)
    aged = 0.125 * (L1(0) + L1(1)) + 0.25 * (L2(0) + L2(1)) + 0.125 * (L2(2) + L2(3))
    flat = 0.25 * L1(0) + 0.75 * L2(0)
    raw_frames, n = frames()
    cap = rgb(raw_frames, a.record).astype(float)
    box = lambda im: im.reshape(50, 4, 80, 4, 3).mean((1, 3))
    print('record %d, 4 x 4 box-filtered mean absolute difference to the capture: age-weighted %.2f, single time %.2f'
          ' (WIZ included in the capture only)' % (a.record, np.abs(box(aged) - box(cap)).mean(),
                                                 np.abs(box(flat) - box(cap)).mean()))
    if a.out:
        img = np.concatenate([cap, aged, flat], 1).astype(np.uint8)
        Image.fromarray(img).resize((img.shape[1] * 2, 400), Image.NEAREST).save(a.out)


if __name__ == '__main__':
    main()
