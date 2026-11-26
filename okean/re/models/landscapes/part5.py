"""Model of the fifth part 0x38520 (the blue landscape): state at the part start (snapshot part_38520) plus the
callbacks -> the drawn frames, bit for bit (noise, the sorted edges with the floating horizon, the captions)."""
import numpy as np
from common import *
import textprint

snap = load_snap('part_38520')
M = Mem(snap)
SIN = [M.i32(0x24a3c + 4 * i) for i in range(5121)]
MAPBASE = M.u32(0x37148)
MAP = np.frombuffer(bytes(M.m[C32 + MAPBASE:C32 + MAPBASE + 0x10000]), np.uint8)
ZOFF = M.i32(0x371cc)                      # 5600
SRCPAL = M.arr(0x371da, 768, np.uint8)
HEAP = M.u32(8)                            # getmem high pointer at the part start
P_ADDR = HEAP + 0x3000 + 0x100000          # [0x371b0]: projected vertices (x, y, z), 1024 x 12 bytes

# 0x38585: edges as vertex pairs (the program stores byte offsets = index * 12)
EDGES = [(r * 32 + c, r * 32 + c + 1) for r in range(32) for c in range(31)] + \
        [(c + 32 * i, c + 32 * (i + 1)) for c in range(32) for i in range(31)]
VX = [-4096 + 256 * (k % 32) for k in range(1024)]
VZ = [-4096 + 256 * (k // 32) for k in range(1024)]


def sin(a): return SIN[a]
def cos(a): return SIN[a + 1025]


class State:
    """the part's variables that the callback 0x38985 changes; pos(n) -> (order, row) at callback n"""
    def __init__(self, pos):
        self.pos = pos
        self.n = 0
        g = M.i32
        self.fade = g(0x36e2c)
        self.a = {k: (g(k) + d) & 0xfff for k, d in ((0x37180, 0x2b08), (0x37184, 0x17e8), (0x37188, 0x17e8),
                                                      (0x3718c, 0xe58), (0x37190, 0x1320), (0x37194, 0x2178),
                                                      (0x3715c, 0x2178), (0x37158, 0x8f70))}
        self.zpos = s32(g(0x371d4) - 0x9900)
        # values the first frame sees before any callback ran (from the file)
        self.pitch, self.yaw, self.roll = g(0x37174), g(0x37178), g(0x3717c)
        self.height, self.fx, self.fz = g(0x37150), g(0x3714c), g(0x37154)
        self.mappos = M.u32(0x371d8) & 0xffff

    def callback(self):
        self.n += 1
        order, row = self.pos(self.n)
        if order != 0x12:
            if self.fade != 0x100:
                self.fade += 1
        elif self.fade:
            self.fade -= 1
        a = self.a
        for k, d in ((0x37180, 9), (0x37184, 5), (0x37188, 5), (0x3718c, 3), (0x37190, 4), (0x37194, 7)):
            a[k] = (a[k] + d) & 0xfff
        self.pitch = ((((sin(a[0x37180]) + sin(a[0x37184])) >> 2) + 0x32c) >> 1) & 0xfff
        self.yaw = ((sin(a[0x37188]) + sin(a[0x3718c])) >> 1) & 0xfff
        self.roll = ((sin(a[0x37190]) + sin(a[0x37194])) >> 2) & 0xfff
        a[0x3715c] = (a[0x3715c] + 7) & 0xfff
        self.height = (sin(a[0x3715c]) >> 3) + 0x464
        a[0x37158] = (a[0x37158] + 0x1e) & 0xfff
        xpos = (sin(a[0x37158]) >> 4) + 0x5a00
        self.zpos = s32(self.zpos - 0x20)
        self.fx = xpos & 0xff
        self.fz = self.zpos & 0xff
        self.mappos = ((xpos >> 8) & 0xff) | (((self.zpos & 0xffffffff) >> 8 & 0xff) << 8)


KER = [(0, 0x01010100, 0x00010101), (0x140, 0x02020101, 0x01010202), (0x280, 0x08040201, 0x01020408),
       (0x3c0, 0x0f080201, 0x0102080f), (0x500, 0x0f080201, 0x0102080f), (0x640, 0x08040201, 0x01020408),
       (0x780, 0x02020101, 0x01010202), (0x8c0, 0x01010100, 0x00010101)]


def blob(buf, o):
    for d, v0, v1 in KER:
        for oo, v in ((o + d, v0), (o + d + 4, v1)):
            w = buf[oo] | buf[oo + 1] << 8 | buf[oo + 2] << 16 | buf[oo + 3] << 24
            w = (w + v) & 0xffffffff
            buf[oo] = w & 0xff; buf[oo + 1] = (w >> 8) & 0xff; buf[oo + 2] = (w >> 16) & 0xff; buf[oo + 3] = w >> 24


def line(buf, hor, x1, y1, x2, y2):
    """0x3801c: like 0x350c8, plus the floating horizon hor[320] (a point is drawn only above it)"""
    if not (y1 > 0) and y2 < 0:
        return
    if not ((y1 & 0xffffffff) < (y2 & 0xffffffff)):
        x1, y1, x2, y2 = x2, y2, x1, y1
    ab1 = lambda d: (s32(d) + 1) if s32(d) >= 0 else -s32(d)
    n = max(ab1(x2 - x1), ab1(y2 - y1)) >> 1
    if n == 0:
        return
    sx = idiv_shl8(s32(x2 - x1), n)
    sy = idiv_shl8(s32(y2 - y1), n)
    if y1 < 0:                       # never true
        x1 = s32(x1 + s32(-y1 * sx)); y1 = 0
    X = (x1 << 8) & 0xffffffff
    Y = (y1 << 8) & 0xffffffff
    cl = n & 0xff
    fast = 0 <= y1 <= 0xbf and 0 <= y2 <= 0xbf and 0 <= x1 <= 0x137 and 0 <= x2 <= 0x137
    while True:
        yy, xx = s32(Y) >> 8, s32(X) >> 8
        if fast:
            if yy >= hor[xx]:
                fast = False         # 0x38273 jumps into the slow loop's tail
            else:
                hor[xx] = yy
                blob(buf, yy * 320 + xx)
        elif 0 <= yy <= 0xbf and 0 <= xx <= 0x137 and yy < hor[xx]:
            hor[xx] = yy
            o = yy * 320 + xx
            blob(buf, o)
            buf[o] = (buf[o] + 1) & 0xff          # 0x38253 inc byte [edi]
        X = (X + sx) & 0xffffffff
        Y = (Y + sy) & 0xffffffff
        cl = (cl - 1) & 0xff
        if cl == 0:
            return


class Renderer:
    def __init__(self):
        self.seed = M.u32(0x37130)
        # [0x371b0] is fresh getmem memory: its old contents are the screen coordinates of vertices never projected
        P = np.frombuffer(bytes(M.m[C32 + P_ADDR:C32 + P_ADDR + 0x3000]), '<i4').reshape(1024, 3)
        self.P = [list(map(int, r)) for r in P]
        self.text = textprint.Printer(M)
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
        return bytearray(out.view(np.uint8).tobytes())

    def project(self, st):
        """0x38331"""
        sy_, cy_ = sin(st.yaw), cos(st.yaw)
        sp, cp = sin(st.pitch), cos(st.pitch)
        sr, cr = sin(st.roll), cos(st.roll)
        bl0, bh0 = st.mappos & 0xff, st.mappos >> 8
        for k in range(1024):
            r, c = k // 32, k % 32
            h = (int(MAP[((bh0 - r) & 0xff) << 8 | ((bl0 - c) & 0xff)]) - 0x80) << 5
            x, y, z = VX[k] + st.fx, h + st.height, VZ[k] + st.fz
            x, z = (x * cy_ - sy_ * z) >> 10, (z * cy_ + sy_ * x) >> 10
            y, z = (y * cp - sp * z) >> 10, (z * cp + sp * y) >> 10
            x, y = (x * cr - sr * y) >> 10, (y * cr + sr * x) >> 10
            z += ZOFF
            p = self.P[k]
            if z > 0:
                p[0] = s32(idiv32(s32(x << 8), z) + 0xa0)
                p[1] = s32(idiv32(s32(y << 8), z) + 0x64)
            p[2] = z

    def draw(self, st, textpos):
        """one pass of the main loop 0x3872e..0x388ff; textpos = (order, row) when the printer runs"""
        hor = [200] * 320
        buf = self.noise()
        self.project(st)
        keys = []
        for e, (a, b) in enumerate(EDGES):
            s = s32(self.P[a][2] + self.P[b][2])
            if s > 0:
                keys.append(((s + (e << 16)) & 0xffffffff, e))
        for bit in range(3, 15):         # 0x38834: 12 stable binary partitions = LSD radix sort on bits 3..14
            m = 1 << bit
            keys = [k for k in keys if not k[0] & m] + [k for k in keys if k[0] & m]
        for key, e in keys:
            ei = (key >> 16) & 0xffff
            a, b = EDGES[ei]
            line(buf, hor, self.P[a][0], self.P[a][1], self.P[b][0], self.P[b][1])
        self.text.draw(buf, *textpos)
        self.frames += 1
        return np.frombuffer(bytes(buf), np.uint8).reshape(200, 320)

    @staticmethod
    def palette(fade):
        """0x38912, after the retrace wait: (source * fade) >> 8"""
        return ((SRCPAL.astype(np.int64) * fade) >> 8).astype(np.uint8)


def idiv32(a, d):
    """shl eax, 8 (done by the caller); cdq; idiv: truncating 32-bit division"""
    q = abs(a) // abs(d)
    return -q if (a < 0) != (d < 0) else q
