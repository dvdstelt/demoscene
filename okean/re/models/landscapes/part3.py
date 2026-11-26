"""Model of the third part 0x3554b (the green canyon landscape): renders frame j (the j-th drawn frame) from the
state at the part start (snapshot part_3554b) plus the callback count n whose state the frame is drawn with.

usage: python3 part3.py [first_capture_frame last_capture_frame]   compares with full.frames
"""
import sys
import numpy as np
from common import *

snap = load_snap('part_3554b')
M = Mem(snap)
SIN = [M.i32(0x24a3c + 4 * i) for i in range(5121)]     # sin table; cos = SIN[i + 1025] (0x25a40)
VX = [M.i32(0x2ffa8 + 12 * i) for i in range(256)]
VY = [M.i32(0x2ffa8 + 12 * i + 4) for i in range(256)]
EDGES = [(M.u8(0x30ba8 + 8 * i), M.u8(0x30ba8 + 8 * i + 4)) for i in range(480)]
MAP = M.arr(0x2f2b0, 3200, np.uint8)
ZOFF = M.i32(0x2ff48)        # 1024
PITCH = M.i32(0x2ff50)       # 256
TILT = M.i32(0x2ff54)        # 1024
PAL = M.arr(0x347a8, 1536, np.uint8).reshape(2, 768)


def rot(a, p, q):
    """(p, q) rotated by table angle a: p' = (p*c - s*q) >> 10, q' = (q*c + s*p) >> 10"""
    s, c = SIN[a], SIN[a + 1025]
    return (p * c - s * q) >> 10, (q * c + s * p) >> 10


def h_of(n):
    return ((SIN[(8 * n) & 0xfff] + SIN[(19 * n) & 0xfff]) >> 2) & 0xfff


class State:
    """the callback's variables after n callbacks, given the music position per callback (pos(n) -> (order, row))"""
    def __init__(self, pos):
        self.pos = pos
        self.n = 0
        self.a30 = self.a34 = 0
        self.scroll = 0          # [0x2ff38]
        self.slide = -4096       # [0x2ff40]
        self.row = 0             # [0x2f2ac]
        self.w1, self.w2 = 128, 0   # [0x2f21c], [0x2f220]
        self.hist = []

    def callback(self):
        self.n += 1
        order, row = self.pos(self.n)
        self.a30 = (self.a30 + 8) & 0xfff
        self.a34 = (self.a34 + 0x13) & 0xfff
        self.scroll -= 0x20
        if order == 10 and row >= 16:
            if self.w1:
                self.w1 -= 1
                self.w2 += 1
        else:
            self.scroll &= 0xff
        if self.scroll == 0xe0:
            self.row += 0x10
            if self.row >= 0xc80:
                self.row -= 0xc80
            if self.row < 0:
                self.row += 0xc80
        if order < 10 and self.slide != 0:
            self.slide += 0x40


class Renderer:
    def __init__(self):
        self.seed = M.u32(0x2f224)
        self.hist = [0] * 33          # 0x2f228..0x2f2a8
        self.h = M.i32(0x2ff58)
        self.frames = 0

    def noise(self):
        x = self.seed
        out = np.empty(16000, np.uint32)
        for i in range(16000):
            out[i] = x & 0x0f0f0f0f
            x = (x + 0x9d) & 0xffffffff
            x = ((x >> 11) | (x << 21)) & 0xffffffff
            x ^= 0x9d
        self.seed = x
        return out.view(np.uint8).copy()

    def draw(self, st):
        """one pass of the main loop 0x35590..0x356e3 with the callback state st; returns (indices, palette)"""
        scroll, slide, base = st.scroll, st.slide, st.row
        z = []
        for c in range(16):
            for r in range(16):
                if c in (0, 15):
                    z.append(0x800)
                else:
                    e = base + 0x10 * r + c
                    while e >= 0xc80:
                        e -= 0xc80
                    z.append(int(MAP[e]) * 8 - 0x400)
        buf = self.noise()
        self.hist[32] = self.h
        self.hist = self.hist[1:] + [self.hist[32]]
        s1 = SIN[st.a30] + SIN[st.a34]
        sway = (SIN[(st.a30 + 0xc00) & 0xfff] + SIN[(st.a34 + 0xc00) & 0xfff]) >> 1     # [0x2ff4c]
        self.h = (s1 >> 2) & 0xfff                                                         # [0x2ff58]
        roll = (SIN[(self.hist[0] - self.h) & 0xfff] >> 1) & 0xfff                         # [0x2ff5c]
        P = []
        for i in range(256):
            x, y, zz = VX[i], VY[i] + scroll + slide, z[i]
            y, zz = rot(TILT, y, zz)
            x, y = rot(roll, x, y)
            x -= sway
            x, zz = rot(self.h, x, zz)
            y, zz = rot(PITCH, y, zz)
            zz += ZOFF
            if zz > 0:
                y = idiv_shl8(y, zz)
                x = idiv_shl8(x, zz)
            else:
                k = 1 - zz
                x, y = s32(x * k), s32(y * k)
            P.append((s32(x + 0xa0), s32(y + 0x64)))
        b32 = None
        for a, b in EDGES:
            line(buf, P[a], P[b])
        self.frames += 1
        pal = ((PAL[0].astype(np.int64) * st.w1 + PAL[1].astype(np.int64) * st.w2) >> 7).astype(np.uint8)
        return buf.reshape(200, 320), pal


KER = [(0, 0x01010100, 0x00010101), (0x140, 0x02020101, 0x01010202), (0x280, 0x08040201, 0x01020408),
       (0x3c0, 0x0f080201, 0x0102080f), (0x500, 0x0f080201, 0x0102080f), (0x640, 0x08040201, 0x01020408),
       (0x780, 0x02020101, 0x01010202), (0x8c0, 0x01010100, 0x00010101)]


def blob(buf, o):
    for d, v0, v1 in KER:
        for oo, v in ((o + d, v0), (o + d + 4, v1)):
            w = int(buf[oo]) | int(buf[oo + 1]) << 8 | int(buf[oo + 2]) << 16 | int(buf[oo + 3]) << 24
            w = (w + v) & 0xffffffff
            buf[oo] = w & 0xff; buf[oo + 1] = (w >> 8) & 0xff; buf[oo + 2] = (w >> 16) & 0xff; buf[oo + 3] = w >> 24


def line(buf, p1, p2):
    """0x350c8"""
    (x1, y1), (x2, y2) = p1, p2
    if not (y1 > 0) and y2 < 0:
        return
    if not ((y1 & 0xffffffff) < (y2 & 0xffffffff)):
        x1, y1, x2, y2 = x2, y2, x1, y1
    def ab1(d):
        d = s32(d)
        return d + 1 if d >= 0 else -d
    n = max(ab1(x2 - x1), ab1(y2 - y1)) >> 1
    if n == 0:
        return
    sx = idiv_shl8(s32(x2 - x1), n)
    sy = idiv_shl8(s32(y2 - y1), n)
    if y1 < 0:          # never true (see notes)
        x1 = s32(x1 + s32(-y1 * sx))
        y1 = 0
    X = (x1 << 8) & 0xffffffff
    Y = (y1 << 8) & 0xffffffff
    if 0 <= y1 <= 0xbf and 0 <= y2 <= 0xbf and 0 <= x1 <= 0x137 and 0 <= x2 <= 0x137:
        for i in range(n):
            blob(buf, (s32(Y) >> 8) * 320 + (s32(X) >> 8))
            X = (X + sx) & 0xffffffff
            Y = (Y + sy) & 0xffffffff
        return
    cnt = n & 0xff
    if cnt == 0:
        cnt = 256
    for i in range(cnt):
        yy = s32(Y) >> 8
        xx = s32(X) >> 8
        if 0 <= yy <= 0xbf and 0 <= xx <= 0x137:
            blob(buf, yy * 320 + xx)
        X = (X + sx) & 0xffffffff
        Y = (Y + sy) & 0xffffffff
