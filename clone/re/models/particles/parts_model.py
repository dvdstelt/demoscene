"""Model of the particle parts 1 (0x405527), 3 (0x405c45) and 6 (0x406677), one drawn frame at a time.

The state is read from the emulator's memory (emu.Emu) and the model runs the same frame; check() compares the
model with the original routine run in unicorn on the same state, frame after frame:
  - the particle array (count, every field: the model computes in doubles and stores with truncation to float32,
    the x87 in 64-bit extended with truncation, so a field may differ by an ulp),
  - the random seed, the phases, the 8-bit buffers and the 32-bit picture (rows 0..199, all 320 columns).

usage: python3 parts_model.py part frames [snap] [frame:addr=value ...]
       part 1: snapshot t11148 (default), 3: t7884, 6: t3925; dt from full.flips from the snapshot's tick on;
       pokes set event variables before a frame (value: int, or float with a '.'), e.g. 0:0x422500=0 (event 103),
       40:0x422504=255.0 (event 105: the flash). Prints frames that differ and every 20th frame.
"""
import math
import sys

import numpy as np

from fractions import Fraction

from fpu import f32, f32t, ext
import blur_model

PART = 0xed8450          # particles, 0x34 bytes each
LIST = 0xece810          # screen offsets of the visible particles
BUF0, BUF1 = 0xeaf410, 0xebee10
CUR = 0xf57390           # index of the 'current' 8-bit buffer (always 0)
SEED = 0x420491
DT = 0x420489
COUNT, TARGET = 0x4224f8, 0x4224fc
FIELDS = ['x', 'y', 'z', 'dx', 'dy', 'dz', 'tx', 'ty', 'tz', 'vx', 'vy', 'vz', 'life']
CAP = 10000


def vtrunc32(a):
    """numpy: values stored as float32 with rounding toward zero."""
    a = np.asarray(a, dtype=np.float64)
    f = a.astype(np.float32)
    bad = np.abs(f.astype(np.float64)) > np.abs(a)
    f[bad] = np.nextafter(f[bad], np.float32(0))
    return f.astype(np.float64)


def vdouble_t(a):
    """numpy: x87 extended value stored as a double (fstp qword) - with doubles already, nothing to do."""
    return a


class Rng:
    """0x420491: seed = seed * 0x41c64e6d + 0x3039 (mod 2^32), value = (seed >> 10) & 0x7fff."""
    def __init__(self, seed): self.seed = seed

    def __call__(self):
        self.seed = (self.seed * 0x41c64e6d + 0x3039) & 0xffffffff
        return (self.seed >> 10) & 0x7fff


class State:
    def __init__(self, e):
        self.e = e
        self.mem = np.frombuffer(e.rd(0, 0xf96000), dtype=np.uint8).copy()   # linear memory 0 .. end of BSS
        raw = self.mem[PART:PART + CAP * 0x34].view(np.float32).reshape(CAP, 13).astype(np.float64)
        self.p = {f: raw[:, i].copy() for i, f in enumerate(FIELDS)}
        self.count = self.i32(COUNT)
        self.target = self.i32(TARGET)
        self.rng = Rng(self.u32(SEED))

    def u32(self, a): return int(self.mem[a:a + 4].view(np.uint32)[0])
    def i32(self, a): return int(self.mem[a:a + 4].view(np.int32)[0])
    def f(self, a): return float(self.mem[a:a + 4].view(np.float32)[0])
    def setf(self, a, v): self.mem[a:a + 4] = np.frombuffer(np.float32(v).tobytes(), dtype=np.uint8)
    def seti(self, a, v): self.mem[a:a + 4] = np.frombuffer(np.int32(v).tobytes(), dtype=np.uint8)
    def buf(self, i): a = BUF0 + 0xfa00 * i; return self.mem[a:a + 64000]

    def flush(self):
        """Write the particle array, count, target and seed back into self.mem (for comparisons)."""
        raw = np.stack([self.p[f] for f in FIELDS], axis=1).astype(np.float32)
        self.mem[PART:PART + CAP * 0x34] = raw.reshape(-1).view(np.uint8)
        self.seti(COUNT, self.count)
        self.seti(TARGET, self.target)
        self.mem[SEED:SEED + 4] = np.frombuffer(np.uint32(self.rng.seed).tobytes(), dtype=np.uint8)

    # ---- particle bookkeeping -------------------------------------------------------------------------------
    def age_move(self, dt, move):
        """0x405442 / 0x405b60 (move = True), 0x406615 (move = False): life -= 0.6 dt, pos += 0.3 v dt; a particle
        whose life fell below 0 is replaced by the last one (0x404b0e). Returns the number removed."""
        n = self.count
        p = self.p
        p['life'][:n] = vtrunc32(p['life'][:n] - dt * 0.6)
        if move:
            for c, v in (('x', 'vx'), ('y', 'vy'), ('z', 'vz')):
                p[c][:n] = vtrunc32(p[c][:n] + p[v][:n] * dt * 0.3)
        dead = list(p['life'][:n] < 0)
        i, removed = 0, 0
        keep = ['x', 'y', 'z', 'vx', 'vy', 'vz', 'life']          # 0x404b0e copies these fields only
        while i < n:
            if dead[i]:
                n -= 1
                for f in keep: p[f][i] = p[f][n]
                dead[i] = dead[n]
                removed += 1
            else:
                i += 1
        self.count = n
        return removed

    def spawn1(self, dt):
        """0x4051d5 (part 1): emitter on a 3D Lissajous path; phase += 1.2 dt on every call."""
        ph = self.f(0xf5939c)
        ph = f32t(ph + dt * 1.2)
        self.setf(0xf5939c, ph)
        if self.count >= self.target: return
        i, r, p = self.count, self.rng, self.p
        for c, k in (('x', 0.002), ('y', 0.0042), ('z', 0.0067)):
            o = r() % 48 - 24
            p[c][i] = f32t(math.sin(ph * k) * 200.0 + o)
        for c in ('vx', 'vy', 'vz'):
            p[c][i] = f32t((r() * 5.439512619669277e-05 + -1.0) * 0.03)
        p['life'][i] = float(r() % 696)
        self.count += 1

    def spawn3(self, dt):
        """0x40590c (part 3): random point in a cube of +-48, speed 3 along a random direction."""
        ph = f32t(self.f(0xf5939c) + dt * 0.2)
        self.setf(0xf5939c, ph)
        if self.count >= self.target: return
        i, r, p = self.count, self.rng, self.p
        a = float(r() % 200 - 100); b = float(r() % 200 - 100); c = float(r() % 200 - 100)
        ln = f32t(math.sqrt(a * a + b * b + c * c))
        p['x'][i] = float(r() % 96 - 48)
        p['y'][i] = float(r() % 96 - 48)
        p['z'][i] = float(r() % 96 - 48)
        inv = 1.0 / ln if ln else math.inf
        p['vx'][i] = f32t(a * inv * 3.0)
        p['vy'][i] = f32t(b * inv * 3.0)
        p['vz'][i] = f32t(c * inv * 3.0)
        p['life'][i] = float(r() % 696)
        self.count += 1

    def spawn6(self):
        """0x40628f (part 6): a lattice point (spacing 8) on one face of the cube [-80, 80]^3."""
        if self.count >= self.target: return
        i, r, p = self.count, self.rng, self.p
        face = r() % 6
        u = r() % 20 - 10
        v = r() % 20 - 10
        X, Y, Z = {0: (-10, u, v), 1: (10, u, v), 2: (u, -10, v), 3: (u, 10, v), 4: (v, u, -10), 5: (v, u, 10)}[face]
        p['x'][i], p['y'][i], p['z'][i] = 8.0 * X, 8.0 * Y, 8.0 * Z
        p['life'][i] = float(r() % 96)
        self.count += 1

    # ---- matrix, transform, projection, stamps -----------------------------------------------------------
    @staticmethod
    def matrix(ax, ay, az):
        """I; 0x404f0f(ax) = Rx; 0x404e36(ay) = Ry; 0x404d5c(az) = Rz, each multiplied from the left: M = Rz Ry Rx.
        3x4 row-major float32; sin and cos are stored as float32 first."""
        m = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0]

        def rot(a, r0, r1):          # row r0' = c r0 - s r1, row r1' = c r1 + s r0
            s, c = f32t(math.sin(a)), f32t(math.cos(a))
            for k in range(4):
                u, v = m[4 * r0 + k], m[4 * r1 + k]
                m[4 * r0 + k] = f32t(u * c - v * s)
                m[4 * r1 + k] = f32t(v * c + u * s)
        rot(ax, 1, 2)                # Rx: y' = c y - s z, z' = c z + s y
        rot(ay, 2, 0)                # Ry: z' = c z - s x, x' = c x + s z
        rot(az, 0, 1)                # Rz: x' = c x - s y, y' = c y + s x
        return m

    def transform(self, m, src):
        """0x40507a (src = position) / 0x405162 (src = deformed position): t = M (x, y, z, 1)."""
        n, p = self.count, self.p
        x, y, z = (p[src[0]][:n], p[src[1]][:n], p[src[2]][:n])
        for r, f in enumerate(('tx', 'ty', 'tz')):
            p[f][:n] = vtrunc32(((m[4 * r + 1] * y + m[4 * r] * x) + m[4 * r + 2] * z) + m[4 * r + 3])

    def project(self, k):
        """0x404b84: perspective with a 'fisheye' term k (tx^2 + ty^2); returns the list of screen offsets."""
        n, p = self.count, self.p
        out = []
        for i in range(n):
            tx, ty, tz = p['tx'][i], p['ty'][i], p['tz'][i]
            d = f32t(tz + 300.0)
            if not d > 0: continue
            q = f32t(((tx * tx + ty * ty) * k + d) * 0.0078125)
            inv = f32t(1.0 / q)
            sy = int(ty * inv + 100.0)
            sx = int(tx * inv * 1.2 + 160.0)
            if 0 <= sx <= 316 and 20 <= sy <= 176:
                out.append(sy * 320 + sx)
        return out

    STAMP = {0: np.array([[21, 41, 41, 21], [41, 73, 73, 41], [41, 73, 73, 41], [21, 41, 41, 21]]),
             1: np.array([[3, 5, 5, 3], [5, 6, 6, 5], [5, 6, 6, 5], [3, 5, 5, 3]])}

    def plot(self, k, mode):
        """0x404b84 -> 0x404772 (mode 0) / 0x40489c (mode 1): 4x4 stamps, saturating adds, into buffer 0. Entry 0 of
        the list is never drawn, and nothing is drawn when the list has 2 entries or fewer."""
        lst = self.project(k)
        if len(lst) <= 2: return lst
        b = self.buf(0).reshape(200, 320).astype(np.int32)
        st = self.STAMP[mode]
        for o in lst[1:]:
            y, x = divmod(o, 320)
            b[y:y + 4, x:x + 4] += st
        self.buf(0)[:] = np.minimum(b, 255).reshape(-1)
        return lst

    # ---- 32-bit output -----------------------------------------------------------------------------------
    def palette(self, addr):
        return self.mem[addr:addr + 1024].view(np.uint32)

    def expand(self, src, pal_addr, tgt):
        """0x404a08 over 20 bands x 40 blocks: rows 20..179 of the 8-bit buffer through a 256-dword palette."""
        pal = self.palette(pal_addr)
        tgt[20:180] = pal[src.reshape(200, 320)[20:180]]

    def blit(self, pic_ptr, x, y, level, tgt):
        """0x404002 mode 1: picture {pixels, w, h}; 0xffffffff is transparent; level 0 = the picture as it is,
        1..63: per channel p + (level (d - p)) / 64 (truncated toward zero)."""
        hdr = self.u32(pic_ptr)                                         # the variable holds a pointer to the header
        pix, w, h = (self.u32(hdr + 4 * i) for i in range(3))
        src = self.mem[pix:pix + 4 * w * h].view(np.uint32).reshape(h, w)
        level = min(level, 63)
        d = tgt[y:y + h, x:x + w]
        mask = src != 0xffffffff
        if level <= 0:
            d[mask] = src[mask]
            return
        out = np.zeros_like(d)
        for sh in (0, 8, 16):
            pc = ((src >> sh) & 0xff).astype(np.int64)
            dc = ((d >> sh) & 0xff).astype(np.int64)
            v = level * (dc - pc)
            v = np.where(v >= 0, v >> 6, -((-v) >> 6))
            out |= (((pc + v) & 0xff).astype(np.uint32) << sh)
        out |= src & 0xff000000
        d[mask] = out[mask]

    # ---- parts ---------------------------------------------------------------------------------------------
    def part1(self, tgt):
        dt = self.f(DT)
        if self.u32(0x422510) and 0 < self.f(0x422514):               # title fade (event 106)
            v = f32t(self.f(0x422514) - dt * 0.45)
            self.setf(0x422514, 0.0 if v < 0 else v)
        if self.u32(0x422508) and 0 < self.f(0x42250c):               # credits fade (event 107)
            v = f32t(self.f(0x42250c) - dt * 0.75)
            self.setf(0x42250c, 0.0 if v < 0 else v)
        self.setf(0xf593ac, f32t(dt * 0.02 + self.f(0xf593ac)))       # fisheye phase
        if self.u32(0x422500) == 0 and self.target < 5000:              # growth after event 103
            self.target = min(5000, int(dt * 10.0 + self.target))
        for a, k in ((0xf593a0, 0.001), (0xf593a4, 0.002), (0xf593a8, 0.003)):
            self.setf(a, f32t(self.f(a) + dt * k))
        n_spawn = self.target - self.count + self.age_move(dt, True)
        for _ in range(n_spawn): self.spawn1(dt)
        m = self.matrix(self.f(0xf593a0), self.f(0xf593a4), self.f(0xf593a8))
        self.transform(m, ('x', 'y', 'z'))
        k = f32t(math.sin(self.f(0xf593ac)) * 0.0065 + 0.007)
        self.plot(k, 0)
        if self.u32(0x422500) == 1:
            lv = f32t(math.sin(self.f(0xf593a0) * 13.0) * 0.19 + 0.8)
            ph = blur_model.radial_blur(self.mem, BUF0, 160.0, 100.0, lv, f32(-0.025), 0, self.f(0xeaf40c), dt)
            self.setf(0xeaf40c, ph)
        cur, oth = self.buf(0), self.buf(1)
        oth[:] = ((oth.astype(np.int32) * 15 + cur) >> 4).astype(np.uint8)     # 0x4045b4
        if self.u32(0x422504) & 0x7fffffff:                             # flash (event 105)
            v = f32t(self.f(0x422504) - dt * 2.3)
            v = 0.0 if v < 0 else v
            self.setf(0x422504, v)
            oth[:] = int(v) & 0xff
        self.expand(oth, 0xf57394, tgt)                                 # all eight band palettes are equal
        if self.u32(0x422510):
            self.blit(0xf73930, 141, 104, int(self.f(0x422514)), tgt)
        if self.u32(0x422508):
            self.blit(0xf7392c, 141, 74, int(self.f(0x42250c)), tgt)
        cur[:] = 0

    def part3(self, tgt):
        dt = self.f(DT)
        self.setf(0xf5b3b4, f32t(1.0 - self.f(0xf5b3b4)))               # toggled, never read
        if self.target < 5000 and self.u32(0x422518) == 0:
            self.target = min(5000, int(dt * 5.0 + self.target))
        elif self.target != 0 and self.u32(0x422518) != 0:             # event 301
            self.target = max(0, int(self.target - dt * 50.0))
            self.count = self.target
        for a, k in ((0xf5b3b8, 0.003), (0xf5b3bc, 0.004), (0xf5b3c0, 0.005)):
            self.setf(a, f32t(self.f(a) + dt * k * 1.0051))
        n_spawn = self.target - self.count + self.age_move(dt, True)
        for _ in range(n_spawn): self.spawn3(dt)
        m = self.matrix(self.f(0xf5b3b8), self.f(0xf5b3bc), self.f(0xf5b3c0))
        self.transform(m, ('x', 'y', 'z'))
        self.plot(0.0, 1)
        tgt[0:20] = 0
        tgt[180:200] = 0
        cur, oth = self.buf(0), self.buf(1)
        c = cur.astype(np.int32)
        s = c[0:63360] + c[319:63679] + c[321:63681] + c[640:64000]    # 0x404621, rows 1..198
        oth[320:63680] = (s >> 2).astype(np.uint8)
        self.expand(oth, 0xf593b0 + 7 * 0x400, tgt)
        # 0x405f8c: cur = 3/4 of 'other' magnified by 65536/32000 about (160, 100), nearest pixel
        j = np.arange(200)
        sr = (((j - 100) * 32000) >> 16) + 100
        i = np.arange(320)
        sc = (((i - 160) * 32000) >> 16) + 160
        o2 = oth.reshape(200, 320).astype(np.int32)
        cur[:] = ((o2[sr][:, sc] * 3) >> 2).astype(np.uint8).reshape(-1)
        tgt[135:175, 162:310] = 0
        lv = f32t(float(ext(63 - ext(self.target * Fraction(0.05)))))   # exact x87 arithmetic: at multiples of 20
                                                                        # the double result would be one level off
        lv = 0.0 if lv < 0 else lv
        self.blit(0xe3511c, 212, 160, int(lv), tgt)
        self.blit(0xe350f4, 212, 87, int(lv), tgt)

    def part6(self, tgt):
        dt = self.f(DT)
        for a, k in ((0xf5d3d0, 0.002), (0xf5d3d4, 0.003), (0xf5d3d8, 0.004)):
            self.setf(a, f32t(self.f(a) + dt * k))
        if self.target != 0 and self.u32(0x42252c) != 0:               # event 601
            self.target = max(0, int(self.target - dt * 35.0))
            self.count = self.target
        for _ in range(self.age_move(dt, False)): self.spawn6()
        # 0x40600f: 'breathing' scale of the integer positions
        gx = dt * self.f(0x422520) + self.f(0xf5d3c4)                 # fst: the register keeps the unrounded sum
        g = f32t(gx)
        self.setf(0xf5d3c4, g)
        g8, g13 = f32t(gx * 0.8), f32t(gx * 1.3)
        self.setf(0xf5d3c8, g8); self.setf(0xf5d3cc, g13)
        n, p = self.count, self.p
        ix = np.trunc(p['x'][:n]); iy = np.trunc(p['y'][:n]); iz = np.trunc(p['z'][:n])
        k6, k23 = 0.1666666666666666, 0.6666666666666666         # doubles 0x41a3e0, 0x41a3e8 (k6 is 2 ulp below 1/6)
        msin = np.vectorize(math.sin, otypes=[float])                             # libm, as the emulator's fsin (numpy's may differ)
        a = vdouble_t(msin(ix / 56.0 + g) * k6 + k23)              # kept in a double temporary (truncated)
        b = vdouble_t((msin(iy / 56.0 + g8) * k6 + k23) + a)
        sc = (msin(iz / 56.0 + g13) * k6 + k23) + b
        p['dx'][:n] = vtrunc32(ix * sc); p['dy'][:n] = vtrunc32(iy * sc); p['dz'][:n] = vtrunc32(iz * sc)
        m = self.matrix(self.f(0xf5d3d0), self.f(0xf5d3d4), self.f(0xf5d3d8))
        self.transform(m, ('dx', 'dy', 'dz'))
        self.plot(f32(0.008), 1)
        lv = f32t(math.sin(self.f(0xf5d3d0) * 6.0) * 0.14 + 0.85)
        ph = blur_model.radial_blur(self.mem, BUF0, 160.0, 100.0, lv, f32(-0.025), 1, self.f(0xeaf40c), dt)
        self.setf(0xeaf40c, ph)
        self.expand(self.buf(0), 0xf5b3c4 + 7 * 0x400, tgt)
        self.buf(0)[:] = 0


ROUTINE = {1: 0x405527, 3: 0x405c45, 6: 0x406677}
SNAP = {1: 't11148', 3: 't7884', 6: 't3925'}


def flips_dts(first_tick, n):
    """dt of the capture's frames from first_tick on (full.flips)."""
    import os
    rows = []
    for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'full.flips')):
        if line.startswith('#'): continue
        f = line.split()
        rows.append((int(f[1]), float(f[4])))
    out = []
    for (t0, a), (t1, b) in zip(rows, rows[1:]):
        if t1 >= first_tick: out.append(b - a)
        if len(out) >= n: break
    return out


def check(part, frames, snap=None, pokes=()):
    import emu
    snap = snap or SNAP[part]
    e = emu.Emu(snap)
    tick = int(snap[1:])
    dts = flips_dts(tick, frames)
    pk = {}
    for item in pokes:
        f, rest = item.split(':')
        a, v = rest.split('=')
        pk.setdefault(int(f), []).append((int(a, 0), float(v) if '.' in v else int(v, 0)))
    bad = 0
    s = State(e)
    tgt_addr = e.rd32(0xe350ec)
    tgt = np.frombuffer(e.rd(tgt_addr, 256000), dtype=np.uint32).reshape(200, 320).copy()
    for n, dt in enumerate(dts):
        e.wrf(DT, dt)
        for a, v in pk.get(n, []):
            if isinstance(v, float): e.wrf(a, v)
            else: e.wr32(a, v)
        s = State(e)
        e.call(ROUTINE[part])
        getattr(s, 'part%d' % part)(tgt)
        s.flush()
        et = np.frombuffer(e.rd(tgt_addr, 256000), dtype=np.uint32).reshape(200, 320)
        ec = e.rd32(COUNT)
        ep = np.frombuffer(e.rd(PART, CAP * 0x34), dtype=np.float32).reshape(CAP, 13)[:ec]
        mp = s.mem[PART:PART + CAP * 0x34].view(np.float32).reshape(CAP, 13)[:s.count]
        same_n = ec == s.count
        fields, umax = 'n/a', 1
        if same_n:
            ulp = np.abs(ep.view(np.int32).astype(np.int64) - mp.view(np.int32).astype(np.int64))
            cols = [0, 1, 2, 9, 10, 11, 12, 6, 7, 8] + ([3, 4, 5] if part == 6 else [])
            umax = ulp[:, cols].max(initial=0)
            fields = 'max ulp diff pos %d vel %d life %d t %d' % (
                ulp[:, :3].max(initial=0), ulp[:, 9:12].max(initial=0), ulp[:, 12].max(initial=0),
                ulp[:, 6:9].max(initial=0))
            if part == 6: fields += ' deformed %d' % ulp[:, 3:6].max(initial=0)
        b = [np.count_nonzero(e.buf(BUF0 + 0xfa00 * i, 64000) != s.buf(i)) for i in (0, 1)]
        tdiff = np.count_nonzero(et != tgt)
        ok = same_n and e.rd32(SEED) == s.rng.seed and b == [0, 0] and tdiff == 0 and umax == 0
        bad += not ok
        if ok and n % 20 and n != len(dts) - 1:
            tgt = np.frombuffer(e.rd(tgt_addr, 256000), dtype=np.uint32).reshape(200, 320).copy()
            continue
        print('frame %3d dt %.3f  count emu %d model %d  seed %s  %s  buffers differ %d / %d bytes  picture differs '
              '%d px (max channel %d)' % (
                  n, dt, ec, s.count, 'same' if e.rd32(SEED) == s.rng.seed else 'DIFF', fields, b[0], b[1], tdiff,
                  np.abs(et.view(np.uint8).astype(int) - tgt.view(np.uint8).astype(int)).max()), flush=True)
        # continue from the emulator's state so that a float ulp does not accumulate
        tgt = np.frombuffer(e.rd(tgt_addr, 256000), dtype=np.uint32).reshape(200, 320).copy()
    print('%d of %d frames differ' % (bad, len(dts)))


if __name__ == '__main__':
    args = sys.argv[1:]
    part, frames = int(args[0]), int(args[1])
    snap = args[2] if len(args) > 2 and args[2].startswith('t') else None
    check(part, frames, snap, [a for a in args[2:] if ':' in a])
