"""Reproduce whole captured frames of the 3D parts: render with the model from a render-start dump, run the entry's
2D passes (AA pass 0x2045fa, the credits sprite 0x25f0be, or FIN's blend 0x2046c7) and find the capture record that
equals the result.

usage: python3 test_frame.py DUMPDIR NAME ENTRY
"""
import argparse
import os
import sys

import dumps
import engine

sys.path.insert(0, engine.RE)
from sheet import open_frames, indexed  # noqa: E402

# the credits overlays of entries 1..4: (offset in CREDITS.RAW's pixels, width, height), stride 231
CREDITS = {1: (0x7380, 0x78, 0x40), 2: (0xad40, 0x82, 0x40), 3: (0, 0xe7, 0x40), 4: (0x39c0, 0x97, 0x40)}


def sprite(dst, src, off, w, h, stride):
    """0x25f0be: centred at (160, 100), index 0 transparent."""
    y0 = 100 - (h >> 1)
    x0 = 160 - (w >> 1)
    for y in range(h):
        for x in range(w):
            c = src[off + y * stride + x]
            if c:
                dst[(y0 + y) * 320 + x0 + x] = c


def frame(d, name, entry, prev_vga=None):
    world = engine.World(engine.arch_file('3D/%s.WLD' % name))
    cam, camf = dumps.state(d, world)
    r = dumps.renderer(d, world)

    class A:
        pass
    an = A()
    an.cam = cam
    buf = r.render(an)
    T = d.rd(0x20643c, 0x10000)
    if entry in CREDITS:
        dst = bytearray(d.buf(0x205e88))
        engine.aa_pass(buf, dst, T)
        off, w, h = CREDITS[entry]
        sprite(dst, d.rd(d.u32(0x205e80), 256 * 231), off, w, h, 231)
        return bytes(dst)
    if entry == 26:
        scr = bytearray(prev_vga)
        engine.fin_pass(buf, scr, T)
        return bytes(scr)
    if entry == 11:
        return bytes(buf)
    dst = bytearray(d.rd(0xa0000, 64000))
    engine.aa_pass(buf, dst, T)
    return bytes(dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('name')
    ap.add_argument('entry', type=int)
    a = ap.parse_args()
    raw, nrec = open_frames(os.path.join(engine.RE, 'full.frames'))
    for ps, pe in dumps.pairs(a.folder):
        d = dumps.Dump(ps)
        prev = None
        if a.entry == 26:
            prev = d.rd(0xa0000, 64000)
        img = frame(d, a.name, a.entry, prev)
        best = None
        for k in range(d.frame - 2, min(d.frame + 6, nrec)):
            ref = indexed(raw, k)[0].tobytes()
            n = sum(1 for x, y in zip(img, ref) if x != y)
            if best is None or n < best[1]:
                best = (k, n)
        print('render at refresh %d: best capture record %d, %d pixels differ' % (d.frame, best[0], best[1]))


if __name__ == '__main__':
    main()
