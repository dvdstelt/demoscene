"""Model of the frame copy of p3, p5, p6, p7 (p3 2b1c, p5 2adc, p6 2d5c, p7 2f8c): wait for the retrace flag, the
70 x 44 label into the target at row 9, target rows 1..201 to A000, background picture back into the target.
Input: the 'drawn' dump (just before the copy) and the 'post' dump (just after).

usage: python3 models/engine/check_copy.py <part> models/engine/dumps_<part>/fNNNNN
"""
import glob
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpview import Dump

# part: (target variable, label x variable, label segment (('seg', s) fixed or ('var', cs offset)), background seg)
COPY = {
    'p3': (0x19d0, 0x19c4, ('seg', 0x3342), 0x4bb1),
    'p5': (0x1564, 0x1558, ('var', 0x1556), 0x3f00),
    'p6': (0x177a, 0x1768, ('var', 0x176c), 0x0c97),
    'p7': (0x19d4, 0x19c8, ('var', 0x19c6), 0x5ab4),
}


def main():
    part, stem = sys.argv[1], sys.argv[2]
    f = lambda name: glob.glob(stem + '_*_%s.npz' % name)[0]
    a, b = Dump(f('drawn'), part), Dump(f('post'), part)
    tv, lv, ls, bg = COPY[part]
    tseg = a.seg_of(a.cs16(tv))
    tgt = bytearray(a.b(tseg, 0, 0x10000))
    lab = b.cs16(lv)                       # the per-retrace routine may move it while the copy waits
    if not lab & 0x8000:
        lseg = ls[1] if ls[0] == 'seg' else b.seg_of(b.cs16(ls[1]))
        src = a.b(lseg, 0, 0x10000)
        di, ax = 0xb40, lab
        if ax > 0x46:
            di += ax - 0x46
            ax = 0x46
        dx = 0x46 - ax
        si = 0x30a + dx
        for row in range(0x2c):
            tgt[di:di + ax] = src[si:si + ax]
            si += ax + dx
            di += 0x140
    scr = bytes(tgt[0x140:0x140 + 64000])
    tgt[0x140:0x140 + 64320] = a.b(bg, 0, 64320)
    print('%s frame %d: target %04x, label x %d, background %04x' % (part, a.frame, tseg, (lab ^ 0x8000) - 0x8000, bg))
    for name, mine, ref in (('screen A000', scr, bytes(b.vram[:64000])),
                            ('target', bytes(tgt[:64320]), b.b(tseg, 0, 64320))):
        x, y = np.frombuffer(mine, np.uint8), np.frombuffer(ref, np.uint8)
        print('  %-12s %d of %d bytes differ' % (name, (x != y).sum(), x.size))


if __name__ == '__main__':
    main()
