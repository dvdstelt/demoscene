"""Model of the seventh part 0x5334f ("the curling sheet") of KORSO2.EXE.

Reproduces the part's drawn frames (320x200 indices + the 768-byte DAC palette) from the state at the part's start
(snaps/part_5334f.snap: tables, the noise seed, the text printer's pointer) plus the number of callbacks run and the
music position the main loop sees.

Routines modelled:
  0x5334f  the part: set-up, main loop (grid heights, noise, palette fade / cross-fade, copy, exit when the fade is 0)
  0x5355a  the retrace callback (counters, fade, fall of the sheet in order 27)
  0x531fe  rotate + project the 16x16 grid, draw its 480 edges
  0x52fed  the line drawer: 8x8 additive "glow" stamps every 2 pixels, dword adds (carries between bytes)
  0x4cacc  the shared text printer (core scope), as this part uses it: one page per call, '@' ends a page

usage (in the toolbox):  python3 sheet_model.py               # compare model frames with full.frames
"""
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
C32 = 0x8fc0
M32 = 0xffffffff


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def idiv(dividend_lo, edx, divisor):
    """x86 idiv: edx:eax / divisor, truncating; edx:eax given as the low dword and edx"""
    num = s32(edx) * (1 << 32) + (dividend_lo & M32)
    q = abs(num) // abs(divisor)
    if (num < 0) != (divisor < 0):
        q = -q
    assert -(1 << 31) <= q < (1 << 31), 'divide error'
    return q


def div8(v, d):
    """cdq; shl eax, 8; idiv d  (edx is the sign of v before the shift)"""
    return idiv((v << 8) & M32, -1 if v < 0 else 0, d)


class Tables:
    def __init__(self, snap_path=None):
        import unicorn  # noqa: F401  (unpickling the snapshot needs it)
        snap = pickle.load(open(snap_path or os.path.join(RE, 'snaps', 'part_5334f.snap'), 'rb'))
        mem = snap['mem']

        def rd(a, n):
            return bytes(mem[C32 + a:C32 + a + n])

        def d32(a):
            return struct.unpack('<i', rd(a, 4))[0]
        self.rd, self.d32 = rd, d32
        self.sin = [int(v) for v in np.frombuffer(rd(0x24a3c, 5121 * 4), '<i4')]   # cos = sin[i + 1025] (0x25a40)
        g = np.frombuffer(rd(0x4decd, 256 * 12), '<i4').reshape(256, 3)
        self.gx = [int(v) for v in g[:, 0]]
        self.gy = [int(v) for v in g[:, 1]]
        self.edges = [tuple(int(v) for v in e) for e in np.frombuffer(rd(0x4eacd, 480 * 8), '<i4').reshape(480, 2)]
        self.rowtab = [int(v) for v in np.frombuffer(rd(0x52ccd, 200 * 4), '<i4')]
        self.pal1 = np.frombuffer(rd(0x526cd, 768), np.uint8).astype(np.int64)
        self.pal2 = np.frombuffer(rd(0x529cd, 768), np.uint8).astype(np.int64)
        self.font = np.frombuffer(rd(0x3cec2, 320 * 38), np.uint8).reshape(38, 320)
        self.cmap = rd(0x4c861, 0x100)              # advance per character code (= glyph widths 0x4c8c2 from 'a')
        self.gwid = rd(0x4c8c2, 0x30)
        self.spacing = d32(0x3ceb0)
        self.seed0 = d32(0x4db5c) & M32
        self.camz = d32(0x4de14)
        self.tilt = 0xa00                            # [0x4de40], an immediate at 0x5343b
        self.script_ptr = d32(0x4c8e4)
        self.prn_order = rd(0x3ceb8, 1)[0]
        self.prn_bit = d32(0x3cebe)
        self.b0 = d32(0x4db54)                       # palette level, 1 in the file
        # the 8x8 stamp of 0x52fed: per row the dwords added at x and x + 4
        self.stamp = [(0x1010100, 0x10101), (0x2020101, 0x1010202), (0x8040201, 0x1020408), (0xf080201, 0x102080f),
                      (0xf080201, 0x102080f), (0x8040201, 0x1020408), (0x2020101, 0x1010202), (0x1010100, 0x10101)]


class State:
    """the variables the callback 0x5355a advances, after `n` callbacks; `end` = callbacks run in order 27"""
    def __init__(self, T, n, end_calls=0):
        # callbacks before order 27: b counts up to 64; in order 27 it counts down (and never up again)
        pre = n - end_calls
        b = min(T.b0 + pre, 64)
        b = max(b - end_calls, 0)
        self.b = b
        self.amp = min(n, 256)                       # [0x4db58]
        self.a4dbf4 = (8 * n) & 0xfff
        self.a4dbf8 = (0x13 * n) & 0xfff              # computed into eax by the main loop and thrown away
        self.a4de6c = (7 * n) & 0xfff
        self.phase = (-0x20 * n) & 0xfff              # [0x4db60]
        self.yvel = 2 * end_calls                     # [0x4d850]
        self.ydrop = end_calls * (end_calls + 1)      # [0x4d84c] = sum of 2k, k = 1..end_calls


def noise(buf, seed):
    """0x533f1: 16000 dwords, x = rol(x + 0x9d, 11) ^ 0x9d, each dword & 0x1f1f1f1f; returns the new seed"""
    x = seed
    out = np.empty(16000, np.uint32)
    for i in range(16000):
        x = (x + 0x9d) & M32
        x = ((x << 11) | (x >> 21)) & M32
        x ^= 0x9d
        out[i] = x
    buf[:] = (out & 0x1f1f1f1f).view(np.uint8)
    return x


def heights(T, st):
    """0x5339b: z of the 16x16 grid: sin[(X^2 + Y^2 + phase) & 0xfff] * amp >> 9, X, Y = -64..56 step 8"""
    z = []
    for i in range(16):
        X = -64 + 8 * i
        for j in range(16):
            Y = -64 + 8 * j
            z.append((T.sin[(X * X + Y * Y + st.phase) & 0xfff] * st.amp) >> 9)
    return z


def angles(T, st):
    a44 = ((T.sin[st.a4dbf4] * 3) >> 3) & 0xfff
    a68 = ((T.sin[st.a4de6c] * 3) >> 3) & 0xfff
    return T.tilt, a44, a68


def project(T, st, z):
    """0x531fe: rotate by [0x4de68] (x,y), [0x4de44] (x,z), [0x4de40] (y,z), add the fall, perspective"""
    a40, a44, a68 = angles(T, st)
    S = T.sin
    s1, c1 = S[a68], S[a68 + 1025]
    s2, c2 = S[a44], S[a44 + 1025]
    s3, c3 = S[a40], S[a40 + 1025]
    pts = []
    for k in range(256):
        x, y, zz = T.gx[k], T.gy[k], z[k]
        x1 = s32(x * c1 - y * s1) >> 10
        y1 = s32(y * c1 + x * s1) >> 10
        x2 = s32(x1 * c2 - zz * s2) >> 10
        z1 = s32(zz * c2 + x1 * s2) >> 10
        y2 = s32(y1 * c3 - z1 * s3) >> 10
        z2 = s32(z1 * c3 + y1 * s3) >> 10
        y2 = s32(y2 + st.ydrop)
        z2 = s32(z2 + T.camz)
        if z2 > 0:
            px, py = div8(x2, z2), div8(y2, z2)
        else:
            m = 1 - z2
            px, py = s32(x2 * m), s32(y2 * m)
        pts.append((s32(px + 0xa0), s32(py + 0x64)))
    return pts


def line(T, buf32, x0, y0, x1, y1):
    """0x52fed; buf32 is a Python bytearray of the 64000-byte buffer"""
    if not y0 > 0 and y1 < 0:
        return
    if not x0 > 0 and x1 < 0:
        return
    if not x0 < 0x13f and x1 > 0x13f:
        return
    if not y0 < 0xc7 and y1 > 0xc7:
        return
    if not (y0 & M32) < (y1 & M32):
        x0, y0, x1, y1 = x1, y1, x0, y0
    dx = s32(x1 - x0)
    nx = (dx + 1 if dx >= 0 else -dx) & M32         # cdq; xor eax, edx; inc eax
    dy = s32(y1 - y0)
    ny = (dy + 1 if dy >= 0 else -dy) & M32
    n = max(nx, ny) >> 1
    if n == 0:
        return
    sx = div8(dx, n)
    sy = div8(dy, n)
    if y0 < 0:                                       # never reached: y0 < 0 here means both ends above the screen
        x0 = s32(x0 + (-y0) * sx)
        y0 = 0
    fx = (x0 << 8) & M32
    fy = (y0 << 8) & M32
    cnt = ((n - 1) & 0xff) + 1                       # dec cl; jne
    rt = T.rowtab
    stamp = T.stamp
    for _ in range(cnt):
        py = s32(fy) >> 8
        px = s32(fx) >> 8
        if 0 <= py <= 0xbf and 0 <= px <= 0x137:
            o = rt[py] + px
            for a, b in stamp:
                v = int.from_bytes(buf32[o:o + 4], 'little') + a
                buf32[o:o + 4] = (v & M32).to_bytes(4, 'little')
                v = int.from_bytes(buf32[o + 4:o + 8], 'little') + b
                buf32[o + 4:o + 8] = (v & M32).to_bytes(4, 'little')
                o += 320
        fy = (fy + sy) & M32
        fx = (fx + sx) & M32


class Printer:
    """0x4cacc as this part uses it (the core scope describes the printer)"""
    def __init__(self, T):
        self.T = T
        self.ptr = T.script_ptr
        self.order = T.prn_order
        self.bit = T.prn_bit

    def call(self, buf, order, row):
        T = self.T
        start = self.ptr
        p = start
        pos = 0
        while True:
            ch = T.rd(p, 1)[0]
            if ch == 0x40:                              # '@': end of the page
                p += 1
                bit = row & 0x10
                if order == self.order:
                    if bit == self.bit:
                        self.ptr = start                # same page again next time
                        return
                    self.bit = bit
                else:
                    self.order = order
                    self.bit = bit
                self.ptr = p                            # next call prints the next page
                return
            if ch == 0x0d:                              # new line, centred at row 162
                p += 1
                q, w = p, 0
                while True:
                    c = T.rd(q, 1)[0]
                    if c == 0x0d:
                        break
                    if c == 0x20:
                        w += 4
                    else:
                        if 0x30 <= c <= 0x39:
                            c = c - 0x30 + 0x7b
                        w += T.cmap[c]
                    q += 1
                pos = 0xa0 - (w >> 1) + 0xca80
                continue
            assert ch != 0, 'the printer loops for ever on a 0 byte'
            if ch == 0x20:
                pos += 4 + T.spacing
                p += 1
                continue
            c = ch - 0x30 + 0x7b if 0x30 <= ch <= 0x39 else ch
            adv = T.cmap[c]
            g = c - 0x61
            w = T.gwid[g]
            src = g + sum(T.gwid[k] - 1 for k in range(g))     # = sum of the widths before it
            for r in range(38):
                for cc in range(w):
                    v = T.font[r, src + cc] if src + cc < 320 else T.rd(0x3cec2 + r * 320 + src + cc, 1)[0]
                    if v:
                        buf[pos + r * 320 + cc] = v
            pos += adv + T.spacing
            p += 1


def palette(T, st, order):
    """the main loop's palette (0x53493): fade in pal1 * b >> 6; in order 27 cross-fade to pal2"""
    if order == 0x1b:
        p = (T.pal1 * st.b + T.pal2 * (64 - st.b)) >> 6
    else:
        p = (T.pal1 * st.b) >> 6
    return p.astype(np.uint8)


def draw(T, st, seed, printer, order, row):
    """one pass of the main loop: returns (64000 indices, palette, new seed)"""
    buf = np.empty(64000, np.uint8)
    z = heights(T, st)
    seed = noise(buf, seed)
    pts = project(T, st, z)
    b = bytearray(buf.tobytes())
    for e0, e1 in T.edges:
        line(T, b, pts[e0][0], pts[e0][1], pts[e1][0], pts[e1][1])
    buf = np.frombuffer(bytes(b), np.uint8).copy()
    printer.call(buf, order, row)
    return buf, palette(T, st, order), seed


if __name__ == '__main__':
    sys.path.insert(0, RE)
    from sheet import open_frames
    T = Tables()
    raw, W, H, rec, hdr, nfr = open_frames(os.path.join(RE, 'full.frames'))

    def cap(k):
        o = hdr + k * rec
        pal = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8)
        idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320]
        return idx.reshape(-1), pal
    K0 = 6896
    seed = T.seed0
    pr = Printer(T)
    rows = {}
    for line_ in open(os.path.join(RE, 'full.ticks')):
        if line_[0] != '#':
            f = line_.split()
            rows[int(f[0])] = (int(f[2]), int(f[3]))
    for d in range(0, int(sys.argv[1]) if len(sys.argv) > 1 else 20):
        order, row = rows[K0 + d - 1]
        st = State(T, d)
        img, pal, seed = draw(T, st, seed, pr, order, row)
        ci, cp = cap(K0 + d)
        print(d, K0 + d, 'pixels differ', int((ci != img).sum()), 'pal differ', int((cp != pal).sum()))
