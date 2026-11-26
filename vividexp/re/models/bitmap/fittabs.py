"""Regenerate every 1x1 table of the bitmap effects from its formula and count the bytes that match the file.

    python3 models/bitmap/fittabs.py

Pixel (x, y) of the 320 x 200 table, dx = x - cx, dy = y - cy (integers), r = sqrt(dx^2 + dy^2), a = atan2(dy, dx)
(radians, -pi..pi, y down). The generator stored (int)v & 255 (C conversion: truncation toward zero, then the low
byte). r and a were float variables in most tables (rounded to float32), the rest double.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tabfit import tab  # noqa: E402

F32 = np.float32


def polar(cx, cy, r32=True, a32=True, xs=None, ys=None):
    y, x = np.mgrid[0:200, 0:320]
    if xs is not None:
        x = xs(x)
    if ys is not None:
        y = ys(y)
    dx = (x - cx).astype(float)
    dy = (y - cy).astype(float)
    r = np.sqrt(dx * dx + dy * dy)
    a = np.arctan2(dy, dx)
    if r32:
        r = r.astype(F32).astype(float)
    if a32:
        a = a.astype(F32).astype(float)
    return r, a


def byte(v):
    return np.trunc(v).astype(np.int64) & 255


# the base functions (screen centre (160, 100) unless given)
def f_tab2(r, a):
    return r                                          # distance


def f_tab10(r, a):
    return r + 4 * np.sin(40 * a)                     # distance with 40 small waves around


def f_tab11(r, a):
    return r + 52 * np.sin(10 * a)                    # distance with 10 large waves (petals near the centre)


def f_tab4(r, a):
    return a * (128 / np.pi) + 41 * np.sin(r * (np.pi / 128))     # angle (256 per turn) twisted by the distance


def f_tab13(r, a):
    return a * (128 / np.pi) - 15 * np.sin(r * (np.pi / 12.8))    # angle with a fast radial wave


def f_tab14(r, a):
    return r + 8 * np.sin(100 * a)                    # distance with 100 waves around


def tiles(sx, sy, mirror=False):
    """sub-sampled copies: table(x, y) = base((sx*x') mod 320, (sy*y') mod 200) with x' = x, or, in mirrored tiles
    (mirror=True: the right tile column and the bottom tile row), x' = tile_w - 1 - (x mod tile_w) etc."""
    tw, th = 320 // sx, 200 // sy

    def xs(x):
        xl = x % tw
        if mirror:
            xl = np.where(x // tw == 1, tw - 1 - xl, xl)
        return (sx * xl) % 320

    def ys(y):
        yl = y % th
        if mirror:
            yl = np.where(y // th == 1, th - 1 - yl, yl)
        return (sy * yl) % 200
    return xs, ys


CASES = [
    # file, base, centre, r float32, a float32, tiling (sx, sy, mirrored), flip (mirror of the whole table)
    ('tab2.1x1', f_tab2, (160, 100), True, True, None),
    ('tab4.1x1', f_tab4, (160, 100), True, True, None),
    ('tab10.1x1', f_tab10, (160, 100), True, True, None),
    ('tab11.1x1', f_tab11, (160, 100), True, True, None),
    ('2tab2.1x1', f_tab2, (160, 100), True, True, (1, 2, False)),
    ('2tab4.1x1', f_tab4, (160, 100), True, True, (1, 2, True)),
    ('4tab2.1x1', f_tab2, (160, 100), True, True, (2, 2, False)),
    ('4tab4.1x1', f_tab4, (160, 100), True, True, (2, 2, True)),
    ('4tab10.1x1', f_tab10, (160, 100), True, True, (2, 2, False)),
    ('4tab11.1x1', f_tab11, (160, 100), True, True, (2, 2, False)),
    ('16tab2.1x1', f_tab2, (160, 100), True, True, (4, 4, False)),
    ('16tab4.1x1', f_tab4, (160, 100), True, True, (4, 4, False)),
    ('16tab10.1x1', f_tab10, (160, 100), True, True, (4, 4, False)),
    ('16tab11.1x1', f_tab11, (160, 100), True, True, (4, 4, False)),
    ('tab13_1.1x1', f_tab13, (220, 130), True, False, 'id'),
    ('tab13_2.1x1', f_tab13, (220, 130), True, False, 'xy'),
    ('tab13_3.1x1', f_tab13, (220, 130), True, False, 'x'),
    ('tab13_4.1x1', f_tab13, (220, 130), True, False, 'y'),
    ('tab14.1x1', f_tab14, (179, 40), True, True, None),
]


def main():
    for name, f, (cx, cy), r32, a32, til in CASES:
        xs = ys = None
        if isinstance(til, tuple):
            xs, ys = tiles(*til)
        elif til in ('x', 'xy'):
            xs = (lambda x: 319 - x)
        if til in ('y', 'xy'):
            ys = (lambda y: 199 - y)
        r, a = polar(cx, cy, r32, a32, xs, ys)
        t = tab(name)
        b = byte(f(r, a))
        e = (b - t + 128) % 256 - 128
        print('%-12s %5d / 64000 bytes exact, max |error| %d' % (name, (e == 0).sum(), np.abs(e).max()))


if __name__ == '__main__':
    main()
