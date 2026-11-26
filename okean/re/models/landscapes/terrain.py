"""Model of the set-up 0x38469: the 256x256 height map made by 0x374da (midpoint displacement) and 0x377b2
(in-place smoothing). Compared with the map in snaps/part_2c26f.snap (taken after 0x38469 returned)."""
import sys
import numpy as np
from common import *

sys.setrecursionlimit(10000)


class Gen:
    def __init__(self, seed):
        self.seed = seed
        self.m = bytearray(0x10000)
        self.calls = 0

    def rnd(self):
        x = (self.seed + 0x3b76) & 0xffffffff
        x = ((x >> 13) | (x << 19)) & 0xffffffff
        self.seed = x ^ 0x2cfb
        return self.seed

    def mid(self, a, b, size):
        r = self.rnd()
        d = ((r & 0xff) - 0x80) * size >> 5
        v = ((a + b) >> 1) - d
        if not v > 0x1e:
            v = 0x1e
        elif not v < 0xe1:
            v = 0xe1
        return v & 0xff

    def sub(self, x0, x1, y0, y1):
        """0x374da: x0, x1 columns (eax, ecx), y0, y1 rows (in [0x371c4] / [0x371c8] as row * 256)"""
        size = x1 - x0
        h = size >> 1
        if h == 0:
            return
        xm, ym = x0 + h, y0 + ((y1 - y0) >> 1)
        m = self.m
        R = lambda y, x: ((y & 0xff) << 8) | (x & 0xff)
        c00, c10, c11, c01 = m[R(y0, x0)], m[R(y0, x1)], m[R(y1, x1)], m[R(y1, x0)]
        if m[R(ym, x0)] == 0:
            m[R(ym, x0)] = self.mid(c00, c01, size)
        if m[R(ym, x1)] == 0:
            m[R(ym, x1)] = self.mid(c10, c11, size)
        if m[R(y0, xm)] == 0:
            m[R(y0, xm)] = self.mid(c00, c10, size)
        if m[R(y1, xm)] == 0:
            m[R(y1, xm)] = self.mid(c01, c11, size)
        m[R(ym, xm)] = ((c00 + c10 + c01 + c11) >> 2) & 0xff
        self.sub(x0, xm, y0, ym)
        self.sub(x0, xm, ym, y1)
        self.sub(xm, x1, ym, y1)
        self.sub(xm, x1, y0, ym)


def smooth(m, after=0, edx_hi=0):
    """0x377b2: in place, i = 0..0xffff: m[i] = ((m[i] + 2 m[i+1] + m[i-1] + m[i-256]) * 0x33 >> 8) & 0xff
    (the first m[i+1] is read with a word load: for i = 0xffff it is the byte after the map, `after`)"""
    for i in range(0x10000):
        w1 = m[i + 1] if i < 0xffff else after
        e = m[i] + w1 + (m[(i - 1) & 0xffff] | (edx_hi if i == 0 else 0)) + m[(i + 1) & 0xffff] + m[(i - 256) & 0xffff]
        m[i] = ((e * 0x33) >> 8) & 0xff


if __name__ == '__main__':
    s0 = Mem(load_snap('part_38469'))
    s1 = Mem(load_snap('part_2c26f'))
    base = s1.u32(0x37148)
    print('map base code32 %#x (linear %#x), seed file %#x snap0 %#x after %#x' % (
        base, base + C32, fi32(0x37130) & 0xffffffff, s0.u32(0x37130), s1.u32(0x37130)))
    g = Gen(s0.u32(0x37130))
    g.sub(0, 0x100, 0, 0x100)
    print('seed after generator %#x' % g.seed)
    snapmap = np.frombuffer(bytes(s1.m[C32 + base:C32 + base + 0x10000]), np.uint8)
    after = s1.m[C32 + base + 0x10000]
    raw = np.frombuffer(bytes(g.m), np.uint8)
    smooth(g.m, after)
    mm = np.frombuffer(bytes(g.m), np.uint8)
    d = np.nonzero(mm != snapmap)[0]
    print('smoothed map differences:', len(d), d[:10], mm[d[:10]], snapmap[d[:10]])
    np.save('map_raw.npy', raw)
    np.save('map.npy', snapmap)
