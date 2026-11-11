"""Python model of JIZZ.EXE's texture generator ("decrunching gfx": driver 0x12c50, interpreter 0x12e72).

Runs the 17 texture scripts from the unpacked image (jizz_img.bin) and returns what the intro builds: the TGA
images that the /pandora mode writes (op 0x26) and the final buffers (what 0x12c50 leaves in the texture
pointers [0x654e..0x658e]). See notes-textures.md for the pseudo-C.

x87 maths: main sets precision control to 24 bits, so every fadd/fsub/fmul/fdiv/fsqrt result is rounded to a
24-bit mantissa (numpy float32 rounding); fild is exact; fsin/fcos/fpatan give the C library's double result
(that is what unicorn's x87 does, and what the /pandora dump was made with); fldpi is the 64-bit pi; fistp rounds
to nearest even.

usage (inside the toolbox, from jizz/re):
    python3 models/textures/texgen.py            # compare all TGAs with dump/*.tga and the buffers in t440.snap
"""
import math
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
IMG = open(os.path.join(RE, 'jizz_img.bin'), 'rb').read()

PLANE = 0x10000
BUFSIZE = 0x30000

F32 = np.float32
C2PI256 = struct.unpack_from('<f', IMG, 0x141e2)[0]      # float32 2*pi/256 at 0x141e2
F255 = struct.unpack_from('<f', IMG, 0x141e6)[0]         # float32 255.0 at 0x141e6
D2PI16384 = struct.unpack_from('<d', IMG, 0x11596)[0]    # double 2*pi/16384 at 0x11596 (map15 stripes)
PI_EXT = np.longdouble(0xc90fdaa22168c235) * np.longdouble(2.0) ** -62   # fldpi (64-bit mantissa)

# plane selection table 0x12e4a: per (mask byte & 7): first plane, planes skipped between, number of planes
MASKTAB = [tuple(IMG[0x12e4a + 4 * i:0x12e4a + 4 * i + 3]) for i in range(8)]

# driver 0x12c50: (script address, destination pointer variable)
BUILDS = [
    (0x12b5d, 0x6582), (0x12439, 0x654e), (0x124c1, 0x658a), (0x12599, 0x6552), (0x12630, 0x6556),
    (0x126fc, 0x655a), (0x1276f, 0x655e), (0x127f0, 0x6562), (0x12919, 0x6566), (0x12993, 0x656a),
    (0x129d6, 0x656e), (0x129ec, 0x6572), (0x129f7, 0x6576), (0x12a7d, 0x657a), (0x12adc, 0x657e),
    (0x12b5d, 0x6582), (0x12bd9, 0x6586),
]


def f32(x):
    return np.asarray(x, dtype=np.float64).astype(F32).astype(np.float64)


def xsin(a):
    return np.array([math.sin(v) for v in np.ravel(a)]).reshape(np.shape(a))


def xcos(a):
    return np.array([math.cos(v) for v in np.ravel(a)]).reshape(np.shape(a))


def fistp(x):
    return np.rint(x).astype(np.int64)


def mul_ext(a, b):
    """x87 fmul with PC=24 of two values that may have more than 24 bits (double / 64-bit): exact product rounded
    once to 24 bits (computed in 80-bit long double, then rounded to float32)."""
    p = np.asarray(a, dtype=np.longdouble) * np.asarray(b, dtype=np.longdouble)
    return p.astype(F32).astype(np.float64)


class Rng:
    """0x141f6: three 32-bit words [0x141ea], [0x141ee], [0x141f2]; 0x12e72 reseeds before every script."""

    def __init__(self):
        self.s = [0x1234abce, 0x03049291, 0x67fcd9a8]

    def __call__(self):
        M = 0xffffffff
        rol = lambda v, n: ((v << n) | (v >> (32 - n))) & M
        a, b, c = self.s
        a = (a + 0x0e372471) & M
        b = (b + 0x00bc5ea9) & M
        c = (c + 0x00c8ade7) & M
        a ^= b
        sa = a - (1 << 32) if a & 0x80000000 else a
        sc = c - (1 << 32) if c & 0x80000000 else c
        p = (sa * sc) & 0xffffffffffffffff          # imul ecx: edx:eax signed
        a, d = p & M, p >> 32
        d = (rol(d, 13) + a) & M
        c = rol(c, 14)
        d = (d - b) & M
        a ^= d
        b = (b + c) & M
        c ^= d
        a = (a - b) & M
        b = rol(b, 9)
        p = a * c                                    # mul ecx: unsigned
        a, d = p & M, p >> 32
        b ^= d
        a = (a + 0x01e42ac3) & M
        b = (b + 0x005edf04) & M
        c = (c + 0x011a2ec1) & M
        self.s = [b, c, a]
        return a, b, c, d                            # eax, ebx, ecx, edx after the call


def sb(v):
    return v - 256 if v >= 128 else v


class TexGen:
    def __init__(self):
        self.B = [np.zeros(BUFSIZE, np.uint8) for _ in range(5)]   # [0x12425..0x12435]: B0..B3 work, B4 temp
        self.tga = {}

    # ---- helpers -------------------------------------------------------------------------------------------
    def plane(self, b, p):
        return self.B[b][p * PLANE:(p + 1) * PLANE].reshape(256, 256)

    @staticmethod
    def planes(mask):
        first, gap, count = MASKTAB[mask & 7]
        return [first + i * (1 + gap) for i in range(count)]

    def temp(self, w):
        """0x13f84: copy the whole current buffer to B4 (the reads then come from the copy)."""
        self.B[4][:] = self.B[w]
        return self.B[4]

    # ---- the interpreter 0x12e72 -----------------------------------------------------------------------------
    def run(self, script, img=None):
        I = self.img = img if img is not None else IMG
        for i in range(4):
            self.B[i][:] = 0
        self.rng = Rng()
        w = 0
        a = script
        while True:
            op = I[a]
            m = I[a + 1]
            P = self.planes(m)
            if op == 0x00:                          # select B[n]
                w = m & 7; a += 2
            elif op == 0x01:                        # end: copy W (0x30000 bytes) to the destination
                return self.B[w].copy()
            elif op == 0x02:                        # fill planes with a byte
                for p in P: self.plane(w, p)[:] = I[a + 2]
                a += 3
            elif op == 0x03:                        # noise: 0xc000 random dwords over all three planes
                self.op_noise(w); a += 1
            elif op in (0x04, 0x05, 0x06):          # box blur 2x2 / 4x4 / 8x8
                s, sh = {4: (2, 2), 5: (4, 4), 6: (8, 6)}[op]
                self.op_blur(w, P, s, sh); a += 2
            elif op == 0x07:
                self.op_hsv2rgb(w); a += 1
            elif op == 0x08:
                self.op_rgb2hsv(w); a += 1
            elif op == 0x09:                        # add a byte to every byte, as dword adds (carries cross bytes)
                v = I[a + 2] * 0x01010101
                for p in P:
                    d = self.B[w][p * PLANE:(p + 1) * PLANE].view('<u4')
                    d[:] = (d.astype(np.uint64) + v) & 0xffffffff
                a += 3
            elif op == 0x0a:
                self.op_blobs(w, P[0], struct.unpack_from('<H', I, a + 2)[0]); a += 4
            elif op == 0x0b:                        # move the image by (dx, dy), wrapping, all planes
                dx, dy = I[a + 1], I[a + 2]
                t = self.temp(w)
                for p in range(3):
                    self.plane(w, p)[:] = np.roll(t[p * PLANE:(p + 1) * PLANE].reshape(256, 256), (dy, dx), (0, 1))
                a += 3
            elif op in (0x0c, 0x0d):                # scale by k/256, saturate at 255 (0x0c) or wrap (0x0d)
                k = struct.unpack_from('<h', I, a + 2)[0]
                for p in P:
                    pl = self.plane(w, p)
                    v = (pl.astype(np.int64) * k) >> 8
                    if op == 0x0c: v = np.where(v > 0xff, 0xff, v)
                    pl[:] = v & 0xff
                a += 4
            elif op == 0x0e:                        # multiply by B[n]: (src * dst) >> 8
                n = I[a + 2]
                for p in P:
                    pl = self.plane(w, p)
                    pl[:] = (self.plane(n, p).astype(np.int32) * pl) >> 8
                a += 3
            elif op == 0x0f:                        # add B[n] as dwords (carries cross bytes)
                n = I[a + 2]
                for p in P:
                    d = self.B[w][p * PLANE:(p + 1) * PLANE].view('<u4')
                    s = self.B[n][p * PLANE:(p + 1) * PLANE].view('<u4')
                    d[:] = (d.astype(np.uint64) + s) & 0xffffffff
                a += 3
            elif op == 0x10:                        # mix with B[n]: (src*a + dst*(256-a)&255) >> 8
                n, al = I[a + 2], I[a + 3]
                for p in P:
                    pl = self.plane(w, p)
                    pl[:] = (self.plane(n, p).astype(np.int32) * al + pl.astype(np.int32) * ((256 - al) & 255)) >> 8
                a += 4
            elif op == 0x11:                        # mix with B[n] by the alpha plane mask(m) of B[k]
                n, mm, k = I[a + 2], I[a + 3], I[a + 4]
                al = self.plane(k, MASKTAB[mm & 7][0]).astype(np.int32)
                for p in P:
                    pl = self.plane(w, p)
                    mix = (self.plane(n, p).astype(np.int32) * al + pl.astype(np.int32) * ((256 - al) & 255)) >> 8
                    pl[:] = np.where(al == 0, pl, mix)
                a += 5
            elif op == 0x12:                        # emboss: directional slope of W.plane(mask) -> B[n].plane(b)
                ang, s, b, n = I[a + 2], sb(I[a + 3]), I[a + 4], I[a + 5]
                self.op_emboss(w, P[0], ang, s * 64, MASKTAB[b & 7][0], n)
                w = n; a += 6
            elif op == 0x13:                        # saturating add of one plane of B[n] to the masked planes
                b, n = I[a + 2], I[a + 3]
                src = self.plane(n, MASKTAB[b & 7][0]).astype(np.int32)
                for p in P:
                    pl = self.plane(w, p)
                    pl[:] = np.minimum(pl + src, 255)
                a += 4
            elif op == 0x14:
                self.op_sines(w, P[0], [sb(x) for x in I[a + 2:a + 8]], I[a + 8]); a += 9
            elif op == 0x15:                        # displace: W[i] = copy[V[i]*256 + U[i]], U/V = planes 0/1 of B[n]
                n = I[a + 2]
                t = self.temp(w)
                idx = self.plane(n, 0).astype(np.int64) + 256 * self.plane(n, 1).astype(np.int64)
                for p in P:
                    self.plane(w, p)[:] = t[p * PLANE:(p + 1) * PLANE][idx]
                a += 3
            elif op in (0x16, 0x17):
                cnt, size = I[a + 2], I[a + 3]
                pts = struct.unpack_from('<8h', I, a + 4)
                self.op_curve(w, P[0], cnt * 16, size, pts); a += 20
            elif op == 0x18:                        # invert
                for p in P: self.plane(w, p)[:] = ~self.plane(w, p)
                a += 2
            elif op == 0x19:                        # copy masked planes to B[n]
                n = I[a + 2]
                for p in P: self.plane(n, p)[:] = self.plane(w, p)
                a += 3
            elif op == 0x1a:                        # copy plane mask(m) of W to plane mask(b) of B[n]; W = B[n]
                b, n = I[a + 2], I[a + 3]
                self.plane(n, MASKTAB[b & 7][0])[:] = self.plane(w, P[0]).copy()
                w = n; a += 4
            elif op == 0x1b:                        # R += x, G += y (identity displacement offsets)
                x = np.arange(256, dtype=np.uint8)
                self.plane(w, 0)[:] += x[None, :]
                self.plane(w, 1)[:] += x[:, None]
                a += 1
            elif op == 0x1c:
                self.op_cone(w, P[0], I[a + 2]); a += 3
            elif op == 0x1d:
                self.op_clouds(w, P[0], I[a + 2]); a += 3
            elif op == 0x1e:
                self.op_mblur(w, P, I[a + 2], I[a + 3], I[a + 4]); a += 5
            elif op == 0x1f:                        # angle map: atan2(y, x) in 1/256 turns, origin at pixel 0,0
                y, x = np.mgrid[0:256, 0:256]
                y = np.where(y >= 128, y - 256, y); x = np.where(x >= 128, x - 256, x)
                at = np.array([math.atan2(float(yy), float(xx)) for yy, xx in zip(y.ravel(), x.ravel())])
                v = (np.asarray(at, np.longdouble) / np.longdouble(C2PI256)).astype(F32).astype(np.float64)
                self.plane(w, P[0])[:] = (fistp(v) & 0xff).reshape(256, 256)
                a += 2
            elif op == 0x20:                        # 320x200 tiled copy of each plane (rows of 320 bytes)
                t = self.temp(w)
                for p in range(3):
                    src = t[p * PLANE:(p + 1) * PLANE].reshape(256, 256)
                    self.B[w][p * PLANE:p * PLANE + 64000] = src[:200, np.arange(320) & 255].ravel()
                a += 1
            elif op == 0x21:                        # planar -> 16-bit pixels (VBE masks; 5:6:5 here)
                t = self.temp(w).astype(np.uint32)
                r, g, b = t[:PLANE], t[PLANE:2 * PLANE], t[2 * PLANE:]
                rs, gs, bs = _sizes()
                px = ((r >> (8 - rs[0])) << rs[1]) | ((g >> (8 - gs[0])) << gs[1]) | ((b >> (8 - bs[0])) << bs[1])
                self.B[w][:2 * PLANE] = px.astype('<u2').view(np.uint8)
                a += 1
            elif op == 0x22:
                self.op_rle4(w, P[0], struct.unpack_from('<I', I, a + 2)[0]); a += 6
            elif op == 0x23:                        # 3x3 colour matrix, signed bytes / 128, clamp 0..255
                M = [sb(x) for x in I[a + 1:a + 10]]
                t = self.temp(w).astype(np.int64)
                r, g, b = t[:PLANE], t[PLANE:2 * PLANE], t[2 * PLANE:]
                for c in range(3):
                    v = (r * M[3 * c] + g * M[3 * c + 1] + b * M[3 * c + 2]) >> 7
                    self.B[w][c * PLANE:(c + 1) * PLANE] = np.clip(v, 0, 255)
                a += 10
            elif op == 0x24:                        # planar -> interleaved R,G,B bytes
                t = self.temp(w)
                self.B[w][:] = t.reshape(3, PLANE).T.ravel()
                a += 1
            elif op == 0x25:
                a += 1
            elif op == 0x26:                        # /pandora: write W as a 256x256 24-bit TGA
                e = I.index(0, a + 1)
                name = I[a + 1:e].decode('latin1')
                self.tga[name] = self.tga_bytes(w)
                a = e + 1
            else:
                raise ValueError(f'op {op:#x} at {a:#x}')

    # ---- ops -------------------------------------------------------------------------------------------------
    def op_noise(self, w):
        out = np.empty(0xc000, np.uint32)
        for i in range(0xc000):
            out[i] = self.rng()[0]
        self.B[w][:] = out.astype('<u4').view(np.uint8)

    def op_blur(self, w, P, s, sh):
        t = self.temp(w)
        h = s // 2
        for p in P:
            src = t[p * PLANE:(p + 1) * PLANE].reshape(256, 256).astype(np.int32)
            acc = np.zeros((256, 256), np.int32)
            for j in range(s):
                for i in range(s):
                    acc += np.roll(src, (h - j, h - i), (0, 1))     # src[y - h + j][x - h + i]
            self.plane(w, p)[:] = acc >> sh

    def op_hsv2rgb(self, w):
        """0x13378 / 0x13f98: H, S, V in planes R, G, B -> R, G, B."""
        H = self.plane(w, 0).astype(np.int64); S = self.plane(w, 1).astype(np.int64)
        V = self.plane(w, 2).astype(np.int64)
        h6 = H * 6
        sec, f = h6 >> 8, h6 & 255
        fp = np.where(sec & 1 == 0, 255 - f, f)
        t = 255 - ((fp * S) >> 8)
        p1 = (t * (V + 1)) >> 8
        p2 = ((255 - S) * (V + 1)) >> 8
        # sector -> (plane of V, plane of p1, plane of p2), table 0x1403a
        order = [(0, 1, 2), (1, 0, 2), (1, 2, 0), (2, 1, 0), (2, 0, 1), (0, 2, 1)]
        out = np.zeros((3, 256, 256), np.int64)
        for s_, (iv, i1, i2) in enumerate(order):
            m = sec == s_
            out[iv][m] = V[m]; out[i1][m] = p1[m]; out[i2][m] = p2[m]
        for p in range(3): self.plane(w, p)[:] = out[p]

    def op_rgb2hsv(self, w):
        """0x133ad / 0x14088: R, G, B -> H, S, V (into planes R, G, B)."""
        R = self.plane(w, 0).astype(np.int64); G = self.plane(w, 1).astype(np.int64)
        Bc = self.plane(w, 2).astype(np.int64)
        e = (R > G) * 1 + (R > Bc) * 2 + (G > Bc) * 4
        # table 0x14156: per e (R dest, G dest, B dest, sector) with 0 = max [0x141df], 1 = mid, 2 = min
        tab = {0: (2, 1, 0, 3), 1: (1, 2, 0, 4), 2: (0, 1, 1, 0), 3: (0, 2, 1, 5),
               4: (2, 0, 1, 2), 5: (0, 1, 2, 0), 6: (1, 0, 2, 1), 7: (0, 1, 2, 0)}
        mx = np.zeros_like(R); md = np.zeros_like(R); mn = np.zeros_like(R); sec = np.zeros_like(R)
        for k, (dr, dg, db, s_) in tab.items():
            m = e == k
            slots = [mx, md, mn]
            # writes happen in the order R, G, B (a later write to the same slot wins)
            slots[dr][m] = R[m]; slots[dg][m] = G[m]; slots[db][m] = Bc[m]
            sec[m] = s_
        same = mx == mn
        q = 0xff00 // np.where(mx == 0, 1, mx)
        mid2 = 255 - (((md * q) >> 8) & 0xff)
        S = 255 - (((mn * q) >> 8) & 0xff)
        q2 = 0xff00 // np.where(S == 0, 1, S)
        f = 255 - (((mid2 * q2) >> 8) & 0xff)
        fx = np.where(sec & 1 == 1, 255 - f, f)
        H = (sec * 256 + fx) // 6
        self.plane(w, 0)[:] = np.where(same, 0, H & 0xff)
        self.plane(w, 1)[:] = np.where(same, 0, S)
        self.plane(w, 2)[:] = mx

    def op_blobs(self, w, p, n):
        """0x1356a: n random 8x8 discs (rows 4,6,8,8,8,8,6,4 wide) of a random grey."""
        pl = self.plane(w, p)
        rows = [(0, 4), (-1, 6), (-2, 8), (-2, 8), (-2, 8), (-2, 8), (-1, 6), (0, 4)]
        for _ in range(n):
            _, b1, _, _ = self.rng()
            a2 = self.rng()[0]
            x0, y0, v = b1 & 0xff, (b1 >> 8) & 0xff, a2 & 0xff
            for j, (dx, wd) in enumerate(rows):
                pl[(y0 + j) & 255, (x0 + dx + np.arange(wd)) & 255] = v

    def op_emboss(self, w, p, ang, s64, dplane, n):
        """0x139e9: out = clamp(round(dx*Cx + dy*Cy), 0, 0xffff) >> 8 with dx = p[y][x+1]-p[y][x],
        dy = p[y+1][x]-p[y][x]; Cx = k*(-cos t), Cy = k*sin t, k = s*64/(|sin t|+|cos t|), t = ang*2pi/256."""
        th = float(f32(ang * C2PI256))
        sn, cs = math.sin(th), -math.cos(th)
        cy32, cx32 = float(f32(sn)), float(f32(cs))
        den = float((np.longdouble(abs(sn)) + np.longdouble(abs(cs))).astype(F32))
        k = float(f32(s64 / den))
        Cx, Cy = float(f32(k * cx32)), float(f32(k * cy32))
        src = self.plane(w, p).astype(np.int64)
        dx = np.roll(src, -1, 1) - src
        dy = np.roll(src, -1, 0) - src
        v = f32(f32(dx * Cx) + f32(dy * Cy))
        iv = fistp(v)
        out = np.where(iv < 0, 0, np.where(iv >= 0xffff, 0xff, (iv >> 8) & 0xff))
        self.plane(n, dplane)[:] = out

    def op_sines(self, w, p, prm, amp):
        """0x137ea: v = round(R(y) * sin(pi*sin X + Y + Z)), R(y) = amp * sin(pi*sin A + B + C)."""
        c = [float(f32(v * C2PI256)) for v in prm]     # [0x139d1],[0x139d5],[0x139dd],[0x139e1],[0x139d9],[0x139e5]
        dX, dY, dA, dB, dC, dZ = c
        A = B = C = 0.0
        rowR = np.zeros(256); X0 = np.zeros(256); Z0 = np.zeros(256)
        for y in range(256):
            A = float(f32(A + dA))
            t = float(mul_ext(math.sin(A), PI_EXT))
            B = float(f32(B + dB))
            t = float(f32(t + B))
            t = float(f32(t + C))
            rowR[y] = float(mul_ext(math.sin(t), amp))
            C = float(f32(C + dC))
            X0[y] = C
            Z0[y] = A
        X = X0.copy(); Z = Z0.copy(); Y = 0.0
        out = np.zeros((256, 256), np.int64)
        for x in range(256):
            X = f32(X + dX)
            t = mul_ext(xsin(X), PI_EXT)
            Y = float(f32(Y + dY))
            t = f32(t + Y)
            Z = f32(Z + dZ)
            t = f32(t + Z)
            v = mul_ext(xsin(t), rowR)
            out[:, x] = fistp(v)
        self.plane(w, p)[:] = out & 0xff

    def op_curve(self, w, p, n, size, pts):
        """0x1360b: Hermite curve in polar coordinates (r = curve x, angle = curve y in 1/256 turns), squares of
        size x size pixels of 255 at n points t = 1/n, 2/n, ..., 1."""
        pl = self.plane(w, p)
        inv = float(f32(1.0 / n))
        co = []
        for k in range(2):
            p0, p1, t0, t1 = pts[4 * k:4 * k + 4]
            F0 = float(p0); F1 = float(t0)
            F3 = float(2 * (p0 - p1) + t0 + t1)
            F2 = float(-(3 * (p0 - p1) + 2 * t0 + t1))
            co.append((F0, F1, F2, F3))
        (x0, x1, x2, x3), (y0, y1, y2, y3) = co
        t = 0.0
        for _ in range(n):
            t = float(f32(t + inv))
            t2 = float(f32(t * t))
            t3 = float(f32(t2 * t))
            X = float(f32(t3 * x3)); Y = float(f32(t3 * y3))
            X = float(f32(f32(t2 * x2) + X)); Y = float(f32(f32(t2 * y2) + Y))
            xv = float(f32(f32(f32(t * x1) + x0) + X))
            yv = float(f32(f32(f32(t * y1) + y0) + Y))
            ang = float(f32(yv * C2PI256))
            py = int(fistp(-float(mul_ext(math.sin(ang), xv))))
            px = int(fistp(float(mul_ext(math.cos(ang), xv))))
            cx, cy = (px - size) & 255, (py - size) & 255
            pl[np.ix_((cy + np.arange(size)) & 255, (cx + np.arange(size)) & 255)] = 0xff

    def op_cone(self, w, p, r):
        """0x13af4: adds round(255*(r - d)/r) where positive, d = distance to pixel (0,0) (wrapping)."""
        K = -float(f32(F255 / r))
        y, x = np.mgrid[0:256, 0:256]
        y = np.where(y >= 128, y - 256, y).astype(np.float64); x = np.where(x >= 128, x - 256, x).astype(np.float64)
        d = f32(np.sqrt(x * x + y * y))
        v = fistp(f32(f32(d - r) * K))
        pl = self.plane(w, p)
        pl[:] = np.where(v >= 0, (pl.astype(np.int64) + v) & 0xff, pl)

    def op_clouds(self, w, p, k):
        """0x13b5c: midpoint-displacement plasma, tileable, sequential random numbers."""
        pl = np.zeros(PLANE, np.int64)
        rng = self.rng
        pl[0] = rng()[0] & 0xff

        def edge(x, y, dx, dy, ex, ey, half):
            i = ((y + dy) & 255) * 256 + ((x + dx) & 255)
            if pl[i] != 0:
                return int(pl[i])
            r = rng()[0]
            disp = (r % (2 * k + 1)) - k
            v = (disp * half) >> 1
            v += int(pl[(y & 255) * 256 + (x & 255)])
            v += int(pl[((y + ey) & 255) * 256 + ((x + ex) & 255)])
            v >>= 1
            if v < 0: v = 0
            elif v >= 0xfe: v = 0xfe
            v += 1
            pl[i] = v
            return v

        def rec(x, y, size):
            if size <= 1:
                return
            half = size >> 1
            if size == 0x100:
                v = rng()[0] & 0xff
            else:
                s = edge(x, y, half, 0, size, 0, half)                         # top
                s += edge(x, y, 0, half, 0, size, half)                        # left
                s += edge(x + size, y, 0, half, 0, size, half)                 # right
                s += edge(x, y + size, half, 0, size, 0, half)                 # bottom
                v = s >> 2
            pl[((y + half) & 255) * 256 + ((x + half) & 255)] = v & 0xff
            rec(x, y, half)
            rec(x + half, y, half)
            rec(x + half, y + half, half)
            rec(x, y + half, half)

        sys.setrecursionlimit(10000)
        rec(0, 0, 256)
        self.plane(w, p)[:] = (pl & 0xff).reshape(256, 256)

    def op_mblur(self, w, P, dx, dy, n):
        """0x13c7a (op 0x1e, not used by the 17 scripts): average of n samples stepping (dx, dy) (signed bytes),
        starting -(n>>1) steps back. The start offset is a 16-bit word incremented per pixel, so x + offset
        carries into the row (the sample row is one lower where x + ox >= 256)."""
        t = self.temp(w)
        st = (-(n >> 1)) & 0xff
        ox = (sb(dx) * sb(st)) & 0xff; oy = (sb(dy) * sb(st)) & 0xff
        start = ((oy << 8) + ox + np.arange(PLANE)) & 0xffff
        sx, sy = start & 255, start >> 8
        for p in P:
            src = t[p * PLANE:(p + 1) * PLANE]
            acc = np.zeros(PLANE, np.int64)
            for i in range(n):
                acc += src[((sy + i * dy) & 255) * 256 + ((sx + i * dx) & 255)]
            self.B[w][p * PLANE:(p + 1) * PLANE] = (acc & 0xffff) // n

    def op_rle4(self, w, p, addr):
        """0x13df4: 4-bit run-length picture, 51200 bytes (256 x 200) into the plane."""
        out = []
        pos, hi, total = addr, False, 0

        def nib():
            nonlocal pos, hi
            v = (self.img[pos] >> 4) if hi else (self.img[pos] & 15)
            if hi: pos += 1
            hi = not hi
            return v

        while total < 0xc800:
            n = nib()
            if n == 0:
                cnt = nib() | nib() << 4 | nib() << 8 | nib() << 12
                out.append((0, cnt))
            elif n == 15:
                cnt = nib() | nib() << 4
                out.append((255, cnt))
            else:
                cnt = 1
                out.append((n * 17, 1))
            total += cnt
        data = np.concatenate([np.full(c, v, np.uint8) for v, c in out])
        base = p * PLANE
        self.B[w][base:base + len(data)] = data
        self.rle_end = pos + (1 if hi else 0)

    def tga_bytes(self, w):
        hdr = bytes(IMG[0x14349:0x14349 + 18])
        hdr = hdr[:12] + struct.pack('<HH', 256, 256) + hdr[16:]
        b = self.B[w]
        pix = np.stack([b[2 * PLANE:3 * PLANE], b[PLANE:2 * PLANE], b[:PLANE]], 1)
        return hdr + pix.tobytes()


def _sizes():
    """Colour field (size, position) for R, G, B as 0x1f5b8 left them for 0x13d73 (VBE mode 0x10e: 5:6:5)."""
    return (5, 11), (6, 5), (5, 0)


def build_all():
    """Run the driver 0x12c50: returns (tgas {name: bytes}, finals {pointer variable: bytes})."""
    tg = TexGen()
    finals = {}
    for script, dst in BUILDS:
        out = tg.run(script)
        finals[dst] = out
        if script == 0x12a7d:
            finals[0x658e] = tg.B[2][:0x20000].copy()        # 0x12d44: B2 (logo2, 16 bit 320x200) -> [0x658e]
    # 0x12d87: map15 [0x6586]: rows darkened by (150 + round(100*sin(y*576*2pi/16384))) / 256
    m15 = finals[0x6586].copy().reshape(3, 256, 256)
    for y in range(256):
        a = float(mul_ext(float(y * 0x240), D2PI16384))
        s = int(fistp(float(mul_ext(math.sin(a), 100.0))))
        dl = (s + 150) & 0xff
        m15[:, y, :] = (m15[:, y, :].astype(np.int32) * dl) >> 8
    finals[0x6586] = m15.ravel()
    # 0x12e08: [0x6572]: B = R[y+1][x] - R[y-1][x]; G[y][x-1] = R[y][x+1] - R[y][x-1] (bytes, wrapping)
    t = finals[0x6572].copy().reshape(3, 256, 256)
    R = t[0].astype(np.int32)
    t[2] = (np.roll(R, -1, 0) - np.roll(R, 1, 0)) & 0xff
    t[1] = np.roll((np.roll(R, -1, 1) - np.roll(R, 1, 1)) & 0xff, -1, 1)
    finals[0x6572] = t.ravel()
    return tg.tga, finals


if __name__ == '__main__':
    import time
    t0 = time.time()
    tgas, finals = build_all()
    print(f'built in {time.time() - t0:.1f} s')
    ok = True
    for name, data in sorted(tgas.items()):
        ref = open(os.path.join(RE, 'dump', name), 'rb').read()
        same = ref == data
        ok &= same
        nd = sum(1 for a, b in zip(ref, data) if a != b) if not same else 0
        print(f'{name:14s} {"OK" if same else f"DIFF ({nd} bytes)"}')
    try:
        sys.path.insert(0, RE)
        import pickle
        import unicorn  # noqa: F401
        # t430 is still inside 0x12c50 (map15's blur); t440 is a few ticks into the first part, which does not
        # touch the texture buffers
        mem = pickle.load(open(os.path.join(RE, 'snaps', 't440.snap'), 'rb'))['mem']
        sizes = {0x658e: 0x20000}
        for dst, data in sorted(finals.items()):
            ptr = struct.unpack_from('<I', mem, dst)[0]
            n = sizes.get(dst, BUFSIZE)
            same = bytes(mem[ptr:ptr + n]) == bytes(data[:n])
            ok &= same
            print(f'[{dst:#x}] = {ptr:#x}: {"OK" if same else "DIFF"}')
    except FileNotFoundError:
        pass
    print('ALL OK' if ok else 'MISMATCH')
