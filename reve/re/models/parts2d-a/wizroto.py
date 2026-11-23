"""Entry 7 (init 0x204bed, retrace 0x204c5d, frame 0x204cb4): two rotozoom layers and GFX\\WIZ.RAW zooming.

usage: python3 wizroto.py [--snap snaps/f3500.snap --from 3500] [--to 3560] [--verbose]
       python3 wizroto.py --start [--to 4088]       from the entry's start (state from the image, buffer [0x205e8c]
                                                    from f2800: still all zero, as entry 4 left it)

Steps the retrace routine once per refresh and the frame routine once per refresh (the capture draws one frame per
refresh here) and compares the buffer [0x205e8c] after each frame with the capture's next record: record r shows
the frame drawn after r - 3193 retrace calls (the init is short, so the first frame waits for the retrace of
record 3193). Entry 8's init runs at record 4089, so 4088 is the entry's last record.
"""
import argparse

import numpy as np

from common import Mem, snap, frames, indexed, raw, rep
from roto import RotoState, layer1, layer2, sin_word

START = 3193            # full.log: entry 7: retrace routine set at frame 3193


class Wiz:
    def __init__(self, mem=None):
        src = mem.u32 if mem else rep.rd32
        self.a = src(0x2174c8) - (1 << 32 if src(0x2174c8) & 0x80000000 else 0)   # angle (sine byte index)
        self.p = src(0x2174cc)                       # sine byte index of the angle speed
        z = mem.u8(0x217570) if mem else rep.rd(0x217570, 1)[0]
        sp = mem.u8(0x217571) if mem else rep.rd(0x217571, 1)[0]
        self.z, self.speed = z, sp                    # zoom byte, its step (signed byte)
        self.roto = RotoState(mem)

    def retrace(self):
        """0x204c5d, once per refresh."""
        self.p = (self.p + 10) & 0xffffffff
        self.a += sin_word(self.p) >> 9
        self.z = (self.z + self.speed) & 0xff
        if self.speed < 0x80:
            if self.z == 0xf5:
                self.speed = (-self.speed) & 0xff
        elif self.z == 0x32:
            self.speed = (-self.speed) & 0xff


def sprite_zoom(buf, wiz, z):
    """0x204cb4's sprite: 87 x 124 source, a row / column is drawn when the 8-bit accumulator (+= z) carries; the
    top-left corner at (160 - z / 4, 100 - z / 4); index 0 is transparent."""
    y = 100 - (z >> 2)
    x0 = 160 - (z >> 2)
    ch = 0
    for r in range(124):
        ch += z
        if ch < 256:
            continue
        ch -= 256
        cl = 0
        x = x0
        for c in range(87):
            cl += z
            if cl < 256:
                continue
            cl -= 256
            v = wiz[r, c]
            if v:
                buf[y, x] = v
            x += 1
        y += 1


def frame(buf, w, tex16, tex64, wiz):
    layer1(buf, w.roto, w.a, tex16)
    layer2(buf, w.roto, w.a, tex64)
    sprite_zoom(buf, wiz, w.z)


def textures():
    px, _ = raw('GFX\\TEXTURE.RAW')
    return px[96:112, 16:32].reshape(-1), px[64:128, 64:128].reshape(-1)   # 0x6010, 0x4040: init 0x204bed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--snap', default='snaps/f3500.snap')
    ap.add_argument('--from', dest='frm', type=int, default=3500)
    ap.add_argument('--start', action='store_true')
    ap.add_argument('--to', type=int, default=3560)
    ap.add_argument('--first-without-retrace', action='store_true', help='(a wrong hypothesis, for the record)')
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    raw_frames, n = frames()
    wiz, _ = raw('GFX\\WIZ.RAW')
    tex16, tex64 = textures()
    if a.start:
        m = Mem(snap('snaps/f2800.snap'))
        w = Wiz(None)
        f0 = START
    else:
        m = Mem(snap(a.snap))
        w = Wiz(m)
        f0 = a.frm
        assert (m.arr(m.u32(0x205e90), 256) == tex16).all() and (m.arr(m.u32(0x205e94), 4096) == tex64).all()
    buf = m.arr(m.u32(0x205e8c), 64000).reshape(200, 320)
    k = f0
    first = True
    bad = 0
    while k < a.to:
        if not (first and a.first_without_retrace):
            w.retrace()
        first = False
        frame(buf, w, tex16, tex64, wiz)
        k += 1
        px, _ = indexed(raw_frames, k)
        diff = int((px != buf).sum())
        bad += diff != 0
        if diff or a.verbose:
            print('record %d: a %d z %d, %d pixels differ' % (k, w.a, w.z, diff))
    print('records %d..%d: %d differ' % (f0 + 1, a.to, bad))


if __name__ == '__main__':
    main()
