"""Entries 9, 10 (GFX\\PLAYMO.RAW) and 15 (GFX\\BOUCHE.RAW): the picture turned ring by ring.

usage: python3 twirl.py 9|10|15 [--quiet]                 from the entry's start (entries 10 and 15 replay the
                                                       retrace routines of the entries before them)
       python3 twirl.py 15 --snap snaps/f11200.snap      from a snapshot (state and buffer)

Init 0x204de0 / 0x2053cc builds 120 rings of picture pixels (radius 120 down to 1, 1025 angles each, each pixel in
the first ring that reaches it); the retrace routine (0x204ef8 / 0x204f6f / 0x2054c4) steps the phase, the twist
and (entry 15) a second phase per refresh; the frame routine 0x204fe8 / 0x205578 writes every ring's colours shifted
by (phase + i * twist) * n >> 12 places. The model replays the retrace routine from the entry's start, builds the
rings from the RAW file (the sine table from the image; the one word past its end, read at angle 4096, from a
snapshot) and compares every record with the buffer after each frame.
"""
import argparse
import re

import numpy as np

from common import Mem, snap, frames, indexed, raw, rep
from roto import SIN

# full.log: 'retrace routine set at frame' (set) and the next entry's init (end: its first record that is not ours)
ENTRY = {9: dict(pic='GFX\\PLAYMO.RAW', set=6331, end=6777, retrace=(0x320, 4, 0x28)),
         10: dict(pic=None, set=6776, end=7224, retrace=(0x3e8, 8, 0x50)),
         15: dict(pic='GFX\\BOUCHE.RAW', set=10809, end=11702, retrace=(0x320, 4, 0x28))}


def word(mem, a):
    v = mem.u16(a)
    return v - 0x10000 if v & 0x8000 else v


def build_rings(pic, mem):
    """The init's ring lists: [(offsets int array, colours uint8 array)] for i = 0..119 (radius i + 1)."""
    p = pic.reshape(-1).copy()
    rings = [None] * 120
    for r in range(120, 0, -1):
        offs, cols = [], []
        for b in range(0x2000, -1, -8):            # byte offsets into the sine table: angle 4096 - b / 2 ... 0
            y = -((word(mem, SIN + b) * r) >> 15)
            if not -100 < y < 100:
                continue
            x = (word(mem, SIN + 0x800 + b) * r) >> 15
            if not -160 < x < 160:
                continue
            o = (y + 100) * 320 + x + 160
            if p[o] == 0xff:
                continue
            cols.append(p[o])
            p[o] = 0xff
            offs.append(o)
        rings[r - 1] = (np.array(offs, np.int64), np.array(cols, np.uint8))
    return rings


class Twirl:
    def __init__(self, entry, state=None):
        self.limit, self.step_abs, self.dlimit = ENTRY[entry]['retrace']
        self.entry = entry
        if state:
            self.__dict__.update(state)
        else:
            self.step, self.dstep = rep.rd32(0x21758c), rep.rd32(0x217594)        # 4, 1
            self.phase, self.d = rep.rd32(0x217598), rep.rd32(0x2175a0)          # 0, 0
            self.bstep, self.b = rep.rd32(0x217590), rep.rd32(0x2175b0)          # 50, 0 (entry 15)

    def retrace(self):
        """0x204ef8 (entry 9: +-800 by 4, +-40 by 1), 0x204f6f (entry 10: +-1000 by 8, +-80 by 1), 0x2054c4 (entry
        15: as entry 9, and B +-10000 by 50)."""
        self.phase += self.step
        if self.step >= 0:
            if self.phase >= self.limit:
                self.step = -self.step_abs
        elif self.phase <= -self.limit:
            self.step = self.step_abs
        self.d += self.dstep
        if self.dstep >= 0:
            if self.d >= self.dlimit:
                self.dstep = -1
        elif self.d <= -self.dlimit:
            self.dstep = 1
        if self.entry == 15:
            self.b += self.bstep
            if self.bstep >= 0:
                if self.b >= 10000:
                    self.bstep = -50
            elif self.b <= -10000:
                self.bstep = 50

    def frame(self, buf, rings):
        flat = buf.reshape(-1)
        acc = self.b if self.entry == 15 else 0
        for offs, cols in rings:
            n = len(offs)
            if n == 0:
                continue
            e = self.phase + acc
            acc += self.d
            shift = ((e * n) & 0xffffffff)
            shift = (shift - (1 << 32) if shift & 0x80000000 else shift) >> 12
            idx = (shift + np.arange(n)) % n
            flat[offs[idx]] = cols
        if self.entry == 15:
            flat[0x7da0] = flat[0x7da1] = 0x18


def run(t, rings, buf, raw_frames, first_record, nframes, first_has_retrace, mask=None, quiet=False):
    """Draw nframes frames; frame k shows in record first_record + k. Returns the number of records that differ."""
    bad = 0
    for k in range(nframes):
        if k or first_has_retrace:
            t.retrace()
        t.frame(buf, rings)
        r = first_record + k
        px, _ = indexed(raw_frames, r)
        diff = (px != buf) if mask is None else (px != buf) & mask
        nd = int(diff.sum())
        bad += nd != 0
        if nd and not quiet:
            print('record %d: phase %d twist %d b %d: %d pixels differ' % (r, t.phase, t.d, t.b, nd))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('entry', type=int, choices=(9, 10, 15))
    ap.add_argument('--snap', help='start from this snapshot (state and buffer), e.g. snaps/f11200.snap')
    ap.add_argument('--first-has-retrace', type=int, default=None)
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    raw_frames, n = frames()
    mem = Mem(snap('snaps/f6300.snap'))         # the sqrt table at 0x219ec8: the word read at angle 4096
    e = ENTRY[a.entry]
    pic, _ = raw(ENTRY[15 if a.entry == 15 else 9]['pic'])
    rings = build_rings(pic, mem)
    if a.snap:
        m = Mem(snap(a.snap))
        st = dict(step=m.s32(0x21758c), dstep=m.s32(0x217594), phase=m.s32(0x217598), d=m.s32(0x2175a0),
                  bstep=m.s32(0x217590), b=m.s32(0x2175b0))
        t = Twirl(a.entry, st)
        buf = m.arr(m.u32(0x205e8c), 64000).reshape(200, 320)
        f0 = int(re.search(r'f(\d+)', a.snap).group(1))
        bad = run(t, rings, buf, raw_frames, f0 + 1, e['end'] - f0 - 1, True, quiet=a.quiet)
        print('entry %d from %s: records %d..%d, %d differ' % (a.entry, a.snap, f0 + 1, e['end'] - 1, bad))
        return
    t = Twirl(9)
    buf = pic.copy()
    mask = None
    if a.entry == 9:
        fh = 0 if a.first_has_retrace is None else a.first_has_retrace
        first = e['set'] + (1 if fh else 0)
        bad = run(t, rings, buf, raw_frames, first, e['end'] - first, fh, quiet=a.quiet)
        print('entry 9 from its start: records %d..%d, %d differ' % (first, e['end'] - 1, bad))
        return
    for _ in range(ENTRY[10]['set'] - ENTRY[9]['set']):      # entry 9's retrace routine: refreshes 6331..6775
        t.retrace()
    if a.entry == 10:
        t.limit, t.step_abs, t.dlimit = ENTRY[10]['retrace']
        fh = 1 if a.first_has_retrace is None else a.first_has_retrace
        first = e['set'] + (1 if fh else 0)
        bad = run(t, rings, buf, raw_frames, first, e['end'] - first, fh, quiet=a.quiet)
        print('entry 10 after entry 9: records %d..%d, %d differ' % (first, e['end'] - 1, bad))
        return
    # entry 15: entry 10's steps carry over; init 0x2053cc sets phase 120 and twist 92; B starts at the image's 0
    t.limit, t.step_abs, t.dlimit = ENTRY[10]['retrace']
    for _ in range(ENTRY[10]['end'] - ENTRY[10]['set']):
        t.retrace()
    st = dict(step=t.step, dstep=t.dstep, phase=0x78, d=0x5c, bstep=rep.rd32(0x217590), b=rep.rd32(0x2175b0))
    t = Twirl(15, st)                  # entry 15's bounds; dphase keeps entry 10's size until its first bounce
    mask = np.zeros(64000, bool)
    for offs, _ in rings:
        mask[offs] = True
    mask[0x7da0:0x7da2] = True
    mask = mask.reshape(200, 320)
    # the first frame (no retrace call yet) is drawn too late for record 10809, which is still black: compare from
    # the second frame on (one retrace call), record 10810
    fh = 1 if a.first_has_retrace is None else a.first_has_retrace
    first = e['set'] + (1 if fh else 0)
    buf = np.zeros((200, 320), np.uint8)
    bad = run(t, rings, buf, raw_frames, first, e['end'] - first, fh, mask=mask, quiet=a.quiet)
    print('entry 15 from its start (ring pixels only, %d of them): records %d..%d, %d differ' % (
        mask.sum(), first, e['end'] - 1, bad))


if __name__ == '__main__':
    main()
