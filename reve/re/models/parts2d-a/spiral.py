"""Entries 12 and 13 (init 0x20562c / 0x20569c, retrace 0x2056f3, frame 0x20578c): rotozoom layer 1, a zooming
feedback blend through GFX\\SPIRBLUR.PAL, and 30 dots of GFX\\SPIRALE.RAW on a turning spiral.

usage: python3 spiral.py [--snap snaps/f8400.snap] [--to R] [--quiet]
       python3 spiral.py --start [--to R]        from entry 12's start (state from the image and from entry 7's end
                                                 in snapshot f7700, buffer [0x205e8c] cleared)

Steps the retrace routine and the frame routine once per refresh (the capture draws a frame every refresh) and
compares the buffer [0x205e8c] after each frame with the capture's next record.
"""
import argparse
import re

import numpy as np

from common import Mem, snap, frames, indexed, raw, file, rep
from roto import RotoState, layer1, sin_word, cos_word

SET12, SET13, END13 = 8122, 9015, 9911   # full.log: retrace routine set (12, 13), entry 14's init
LIST, LIST13, LIST_END = 0x217518, 0x217540, 0x217570
M32 = 0xffffffff


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


class Spiral:
    def __init__(self, mem=None):
        g = (lambda a: mem.u32(a)) if mem else rep.rd32
        self.a, self.p = s32(g(0x2174c8)), g(0x2174cc)          # rotozoom angle, its speed's sine index
        self.q, self.qprev = s32(g(0x2174ec)), s32(g(0x2174f0))  # spiral angle (4096 = a turn), last refresh's
        self.zoom, self.spd = s32(g(0x2174f4)), s32(g(0x2174f8)) # feedback zoom (1024 = 1), spiral speed (16.16)
        self.ptr, self.cnt = g(0x217508), g(0x21750c)            # key list pointer, refreshes to the next key
        self.inc1, self.inc2 = s32(g(0x217510)), s32(g(0x217514)) # per-refresh increments of spd and zoom
        self.roto = RotoState(mem)

    def retrace(self):
        """0x2056f3, once per refresh."""
        self.p = (self.p + 10) & M32
        self.a = s32(self.a + (sin_word(self.p) >> 8))
        self.qprev = self.q
        self.q = s32(self.q + (self.spd >> 16))
        if self.ptr > LIST_END:
            return
        self.cnt -= 1
        if self.cnt == 0:
            self.cnt = 0xd2
            if self.ptr == LIST_END:
                return
            self.inc1, self.inc2 = s32(rep.rd32(self.ptr)), s32(rep.rd32(self.ptr + 4))
            self.ptr += 8
        self.spd = s32(self.spd + self.inc1)
        self.zoom = s32(self.zoom + self.inc2)


def feedback(buf, zoom, table):
    """0x25eaec, in place: dest = table[src << 8 | dest], src read through a zoom (step = (zoom & 0x3ff) / 1024
    source pixels per pixel) about the centre, rows from the top, blocks of four pixels (read, then write)."""
    step = (zoom << 22) & M32
    h = step >> 16
    edx = (0x1400000 - 320 * h) & M32
    imm = s32(edx) >> 17
    x0 = edx >> 1
    edx = (0xc80000 - 200 * h) & M32
    imm += (s32(edx) >> 17) * 320
    y0 = edx >> 1
    flat = buf.reshape(-1)
    i = np.arange(320, dtype=np.int64)
    colsrc = (x0 + i * step) >> 32                  # carries of the column accumulator before pixel i is read
    for j in range(200):
        rowsrc = (y0 + j * step) >> 32              # carries of the row accumulator before row j
        base = imm + 320 * rowsrc
        d0 = j * 320
        src_idx = base + colsrc
        if src_idx.max() < d0 or src_idx.min() >= d0 + 320:
            s = flat[src_idx].astype(np.int64)      # another row: already new (above) or still old (below)
            flat[d0:d0 + 320] = table[(s << 8) | flat[d0:d0 + 320]]
            continue
        for blk in range(0, 320, 4):                # the source row is this row: emulate the blocks in order
            s = flat[src_idx[blk:blk + 4]].astype(np.int64)
            d = flat[d0 + blk:d0 + blk + 4].astype(np.int64)
            flat[d0 + blk:d0 + blk + 4] = table[(s << 8) | d]


def dots(buf, qprev, dot):
    """0x25f038: 30 dots (the top-left 6 x 6 of SPIRALE.RAW, index 0 transparent) with their top-left corner at
    (160 + (cos(b) * R >> 16), 100 + (sin(b) * R >> 16)), b = (qprev + 300 k) & 0xfff, R = 50 + 5 k; no clipping
    (rows past 199 land in the buffer's slack)."""
    for k in range(30):
        b = ((qprev + 300 * k) & 0xfff) * 2
        R = 50 + 5 * k
        y = 100 + ((sin_word(b) * R) >> 16)
        x = 160 + ((cos_word(b) * R) >> 16)
        for r in range(6):
            if y + r >= 200:
                break
            for c in range(6):
                v = dot[r, c]
                if v:
                    buf[y + r, x + c] = v


def frame(buf, s, tex16, table, dot):
    layer1(buf, s.roto, s.a, tex16)
    feedback(buf, s.zoom, table)
    dots(buf, s.qprev, dot)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--snap', default='snaps/f8400.snap')
    ap.add_argument('--start', action='store_true')
    ap.add_argument('--to', type=int)
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    raw_frames, n = frames()
    tpx, _ = raw('GFX\\TEXTURE.RAW')
    tex16 = tpx[80:96, 16:32].reshape(-1)                      # 0x5010: init 0x20562c
    table = np.frombuffer(file('GFX\\SPIRBLUR.PAL'), np.uint8)
    spx, _ = raw('GFX\\SPIRALE.RAW')
    dot = spx[:6, :6]
    if a.start:
        s = Spiral(None)
        # entry 7 left the rotozoom state and the angle; init 0x20562c does not reset them
        m = Mem(snap('snaps/f7700.snap'))
        s.roto = RotoState(m)
        s.a, s.p = s32(m.u32(0x2174c8)), m.u32(0x2174cc)
        buf = np.zeros((200, 320), np.uint8)      # entry 11 (3D) leaves [0x205e8c] cleared
        f0 = SET12 - 1
    else:
        m = Mem(snap(a.snap))
        s = Spiral(m)
        assert (m.arr(m.u32(0x205e90), 256) == tex16).all()
        assert (m.arr(0x20643c, 65536) == table).all()
        buf = m.arr(m.u32(0x205e8c), 64000).reshape(200, 320)
        f0 = int(re.search(r'f(\d+)', a.snap).group(1))
    to = a.to or END13 - 1
    k = f0
    bad = 0
    while k < to:
        if k == SET13:                     # entry 13's init 0x20569c (before that refresh's retrace routine):
            s.ptr, s.cnt = LIST13, 1       # the key list restarts at its sixth pair, taken at the next refresh
        if not (a.start and k == f0):        # entry 12's init takes three refreshes: its first frame is drawn
            s.retrace()                      # at once, before a retrace call, but is not finished when record
        frame(buf, s, tex16, table, dot)     # 8122 is taken: the comparison starts at 8123 (one retrace call)
        k += 1
        if a.start and k == SET12:
            continue
        px, _ = indexed(raw_frames, k)
        nd = int((px != buf).sum())
        bad += nd != 0
        if nd and not a.quiet:
            print('record %d: zoom %d spd %d q %d a %d: %d pixels differ' % (k, s.zoom, s.spd, s.q, s.a, nd))
    print('records %d..%d: %d differ' % (f0 + 1 + (1 if a.start else 0), to, bad))


if __name__ == '__main__':
    main()
