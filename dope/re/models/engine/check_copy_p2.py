"""Model of p2's frame copy 360c (credit strip by [1904], motion-blur blend through the table at 3527:0000, label,
copy to A000, clear / background) and of the blend-table generators 1967 / 1998. Input: the 'drawn' dump (just before
360c) and the 'post' dump (just after); compares the blur buffer 5517, the screen (A000) and the cleared target 4527.

usage: python3 models/engine/check_copy_p2.py models/engine/dumps_p2/fNNNNN
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpview import Dump


def blend_table(w):
    """1967 (weight w = [1965]) for old, new < 0x80; 1998 for the rest (old < 0x40 or 0x80..0xbf)"""
    t = np.zeros((256, 256), np.int64)
    filled = np.zeros((256, 256), bool)
    old = np.arange(128)[:, None]
    new = np.arange(128)[None, :]
    t[:128, :128] = ((new * w + old * (256 - w)) & 0xffff) >> 8
    filled[:128, :128] = True
    o = np.arange(64)[:, None]
    n = np.arange(128)[None, :]
    ax = n * 0x60 + o * 0x1a0
    v = (((ax >> 1) + (ax & 1)) >> 8) | 0x80
    t[0:64, 128:256] = v            # [bx + 0x80]
    t[128:192, 0:128] = v           # [bx - 0x8000]
    t[128:192, 128:256] = v         # [bx - 0x7f80]
    filled[0:64, 128:] = filled[128:192, :] = True
    return t.astype(np.uint8), filled


def main():
    stem = sys.argv[1]
    a = Dump(stem + '_1_drawn.npz')
    b = Dump(stem + '_2_post.npz')
    cs = lambda o: b.cs16(o)
    tab = np.frombuffer(a.b(0x3527, 0, 0x10000), np.uint8).reshape(256, 256)
    w = a.cs16(0x1965)
    mt, filled = blend_table(w)
    print('blend table: weight [1965] = %d; model differs in %d of %d generated entries; %d entries not generated '
          '(%d of them non-zero in memory)' % (w, (mt[filled] != tab[filled]).sum(), filled.sum(), (~filled).sum(),
                                              (tab[~filled] != 0).sum()))
    tgt = bytearray(a.b(0x4527, 0, 0x10000))
    acc = bytearray(a.b(0x5517, 0, 0x10000))
    k = s = cs(0x1904)
    if k == 0xfffe:
        pass
    elif k == 0xffff:
        tgt[0x6e00:0x9600] = bytes(0x2800)
    elif k == 0:
        tgt[0x6e00:0x9600] = b'\x80' * 0x2800
    else:
        src = (k * 0x2800 + 0xdb0a) & 0xffff
        pics = a.b(0x0451, 0, 0x10000) * 2
        tgt[0x6e00:0x9600] = pics[src:src + 0x2800]
    o = np.frombuffer(bytes(acc[:64000]), np.uint8).astype(np.int64)
    n = np.frombuffer(bytes(tgt[:64000]), np.uint8).astype(np.int64)
    acc[:64000] = tab.ravel()[(o << 8) | n].astype(np.uint8).tobytes()
    lab = b.cs16(0x1955)
    if not lab & 0x8000:
        di, ax = 0xb40, lab
        if ax > 0x46:
            di += ax - 0x46
            ax = 0x46
        dx = 0x46 - ax
        src = a.b(0x2bd2, 0, 0x10000)
        si = 0x30a + dx
        for row in range(0x2c):
            acc[di:di + ax] = src[si:si + ax]
            si += ax + dx
            di += 0x140
    scr = bytes(acc[0x140:0x140 + 64000])
    bg = a.b(0x0ec3, 0, 64320) if b.cs16(0x1947) == 1 else bytes(64320)
    tgt[0x140:0x140 + 64320] = bg
    print('[1904] = %04x, label [1955] = %04x, [1947] = %d' % (s, lab, b.cs16(0x1947)))
    for name, mine, ref in (('blur buffer 5517', bytes(acc[:64320]), b.b(0x5517, 0, 64320)),
                            ('screen A000', scr, bytes(b.vram[:64000])),
                            ('target 4527', bytes(tgt[:64320]), b.b(0x4527, 0, 64320))):
        x, y = np.frombuffer(mine, np.uint8), np.frombuffer(ref, np.uint8)
        print('%-18s %d of %d bytes differ' % (name, (x != y).sum(), x.size))


if __name__ == '__main__':
    main()
