"""Contact sheet of all overlay files rendered as images (for the catalogue): 0xfd00 = palette + 320x200,
0xfa00 = 320x200, 0x10000 = 256x256, others as 256- or 320-wide byte rows; greyscale unless a palette is given.
usage: python3 gallery.py OUTDIR"""
import sys, os
import numpy as np
from PIL import Image, ImageDraw
import cap


def pal_img(idx, pal):
    p = (pal.astype(np.uint32) * 255 // 63).astype(np.uint8)
    return Image.fromarray(p[idx])


def grey(idx):
    return Image.fromarray(idx)


def render(n, pal=None):
    d = np.frombuffer(cap.overlay(n), np.uint8)
    L = len(d)
    if L == 0xfd00:
        return pal_img(d[768:].reshape(200, 320), d[:768].reshape(256, 3))
    if L == 0xfa00:
        a = d.reshape(200, 320)
    elif L == 0x10000:
        a = d.reshape(256, 256)
    elif L >= 0x10000 and L < 0x10400:
        a = d[:0x10000].reshape(256, 256)
    else:
        w = 320 if L % 320 == 0 else 256
        h = L // w
        a = d[:w * h].reshape(h, w)
    if pal is not None:
        return pal_img(a, pal)
    return grey(a)


if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for n in range(2, 59):
        if len(cap.overlay(n)) in (0x300,): continue
        render(n).save(os.path.join(out, 'f%02d.png' % n))
