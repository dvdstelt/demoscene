"""Python model of Paper's 3D scene renderer (0x5eec / 0x3fb3 / 0x43df / 0x18b7 / 0x1943 ...), integer-exact.
Reads all inputs from a memory image (bytes, linear 0 = code32 0) and renders into a 64000-byte buffer."""
import math
import struct

M32 = 0xffffffff


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def sar(v, n):
    return s32(v) >> n


class Mem:
    def __init__(self, mem):
        self.m = bytearray(mem)

    def u32(self, a): return struct.unpack_from('<I', self.m, a)[0]
    def i32(self, a): return struct.unpack_from('<i', self.m, a)[0]
    def w32(self, a, v): struct.pack_into('<I', self.m, a, v & M32)
    def u8(self, a): return self.m[a]
    def i8(self, a): v = self.m[a]; return v - 256 if v >= 128 else v


def imul64(a, b):
    return s32(a) * s32(b)


def shld_hi(prod, n):
    """shld edx, eax, n after imul (edx:eax = prod): returns (prod >> (32 - n)) as 32-bit signed"""
    return s32(prod >> (32 - n))


class Renderer:
    def __init__(self, mem):
        self.M = Mem(mem)
        M = self.M
        self.sin = [M.i32(0x22479 + 4 * i) for i in range(1024)]
        self.recip = lambda n: M.i32(0x1ee15 + 4 * n)
        self.T = M.u32(0x1eded)
        self.back = M.u32(0x1edf9)
        self.wob = M.u32(0x1ee0d)
        self.stroke = M.u32(0x1ee09)

    # ---- matrices
    def b1f(self, a, b):
        S = self.sin
        cb = S[(b + 0x100) & 0x3ff]; sb = S[b & 0x3ff]; ca = S[(a + 0x100) & 0x3ff]; sa = S[a & 0x3ff]
        m = [0] * 9
        m[0] = cb; m[2] = s32(-sb)
        m[3] = shld_hi(imul64(-sb, sa), 16)
        m[6] = shld_hi(imul64(sb, ca), 16)
        m[1] = 0; m[4] = ca; m[7] = sa
        m[5] = s32(-shld_hi(imul64(cb, sa), 16))
        m[8] = shld_hi(imul64(cb, ca), 16)
        return m

    # ---- fill
    def edge(self, x0, y0, x1, y1, tab, mm):
        slot = 0
        if y0 == y1: return
        if y0 > y1:
            slot = 1; x0, x1 = x1, x0; y0, y1 = y1, y0
        if y0 >= 200 or y1 <= 0: return
        dy = y1 - y0
        slope = s32(self.recip(dy) * (x1 - x0)) if dy <= 2048 else s32(self.M.i32(0x1ee15 + 4 * dy) * (x1 - x0))
        if y1 > 200: y1 = 200
        x = s32(x0 << 16)
        if y0 < 0:
            x = s32(x - s32(y0 * slope)); y0 = 0
        mm[0] = min(mm[0], y0); mm[1] = max(mm[1], y1)
        for y in range(y0, y1):
            v = x
            if v < 0: v = 0
            if v > 0x1400000: v = 0x1400000
            tab[y][slot] = v
            x = s32(x + slope)

    def tri(self, pts, col, buf, orop=False):
        tab = self.rowtab
        mm = [0xff, 0]
        (x0, y0), (x1, y1), (x2, y2) = pts
        self.edge(x0, y0, x1, y1, tab, mm)
        self.edge(x1, y1, x2, y2, tab, mm)
        self.edge(x2, y2, x0, y0, tab, mm)
        lo, hi = mm[0] & 0xff, mm[1] & 0xff
        if hi <= lo: return
        for y in range(lo, hi):
            r, l = tab[y][0] >> 16, tab[y][1] >> 16
            if r - l > 0:
                o = y * 320
                if orop:
                    for x in range(l, r): buf[o + x] |= col
                else:
                    buf[o + l:o + r] = bytes([col]) * (r - l)

    # ---- pencil line
    def plot(self, buf, idx, s):
        if 0 <= idx < 64000:
            buf[idx] = self.M.m[self.T + (s << 8) + buf[idx]]
        else:
            self.oob += 1

    def line1612(self, buf, x0, y0, x1, y1):
        M = self.M
        sp = M.u32(0x1ee05)
        eax, ebx, ecx, edx = x0, y0, x1, y1
        edx = edx - ebx
        if edx < 0:
            edx = -edx; ebx = ebx - edx; eax, ecx = ecx, eax
        ecx = ecx - eax
        if ecx < 0:
            if -ecx > edx:
                ecx = -ecx
                return self.xmajor(buf, eax - ecx, ebx + edx, ecx, -edx, sp)
        elif ecx > edx:
            return self.xmajor(buf, eax, ebx, ecx, edx, sp)
        # y-major
        if ebx >= 200 or edx <= 0: return
        slope = s32(self.recip(edx) * ecx)
        x = s32(eax << 16)
        yend = edx + ebx
        if yend <= 0: return
        if yend > 200: yend = 200
        y = ebx
        if y < 0:
            x = s32(x - s32(slope * y)); y = 0
        n = yend - y
        if n <= 0: return
        if slope >= 0:
            if x >= 0x1400000: return
            while x < 0:
                y += 1; x = s32(x + slope)
                if x >= 0: break
                n -= 1
                if n == 0: return
            if n == 0: return
            while True:
                px = x >> 16
                o = y * 320 + px
                self.plot(buf, o, M.u8(sp)); self.plot(buf, o + 1, M.u8(sp + 1)); sp += 2
                y += 1; x = s32(x + slope)
                if x >= 0x1400000: return
                n -= 1
                if n == 0: return
        else:
            if x < 0: return
            while x >= 0x1400000:
                y += 1; x = s32(x + slope)
                if x < 0x1400000: break
                n -= 1
                if n == 0: return
            if n == 0: return
            while True:
                px = x >> 16
                o = y * 320 + px
                self.plot(buf, o, M.u8(sp)); self.plot(buf, o + 1, M.u8(sp + 1)); sp += 2
                y += 1; x = s32(x + slope)
                if x < 0: return
                n -= 1
                if n == 0: return

    def xmajor(self, buf, x, yi, dx, dy, sp):
        M = self.M
        if x >= 320 or dx <= 0: return
        slope = s32(self.recip(dx) * dy)
        y = s32(yi << 16)
        xend = dx + x
        if xend > 320: xend = 320
        if x < 0:
            y = s32(y - s32(slope * x)); x = 0
        n = xend - x
        if n <= 0: return
        if slope >= 0:
            if y >= 0xc80000: return
            while y < 0:
                x += 1; y = s32(y + slope)
                if y >= 0: break
                n -= 1
                if n == 0: return
            if n == 0: return
            row = (y & M32) >> 16
            o = row * 320 + x
            frac = (y << 16) & M32
            st = (slope << 16) & M32
            while True:
                self.plot(buf, o, M.u8(sp)); self.plot(buf, o + 320, M.u8(sp + 1)); sp += 2
                o += 1
                frac += st
                if frac > M32:
                    frac &= M32; o += 320; row += 1
                    if row >= 200: return
                n -= 1
                if n == 0: return
        else:
            if y <= 0: return
            while y >= 0xc80000:
                x += 1; y = s32(y + slope)
                if y < 0xc80000: break
                n -= 1
                if n == 0: return
            if n == 0: return
            row = (y & M32) >> 16
            o = row * 320 + x
            frac = (y << 16) & M32
            st = (-((slope << 16) & M32)) & M32
            while True:
                self.plot(buf, o, M.u8(sp)); self.plot(buf, o + 320, M.u8(sp + 1)); sp += 2
                o += 1
                frac += st
                if frac > M32:
                    frac &= M32; o -= 320; row -= 1
                    if row < 0: return
                n -= 1
                if n == 0: return

    def line18b7(self, buf, x0, y0, x1, y1):
        M = self.M
        ps = self.wob + ((s32(x0 * y0) + y0) & 0x1ff)
        pe = self.wob + ((s32(x1 * y1) + y1) & 0x1ff)
        M.w32(0x1ee05, self.stroke + ((x0 + y0 + x1 + y1) & 0xff))
        for k in range(3):
            ey = y1 + M.i8(pe); ex = x1 + M.i8(pe + 1); pe += 2
            sy = y0 + M.i8(ps); sx = x0 + M.i8(ps + 1); ps += 2
            self.line1612(buf, sx, sy, ex, ey)

    # ---- sky
    def grad(self, buf, r0, r1, c0, c1):
        # 0x1bae: esi=r0, ebp=r1, ebx=c0, edx=c1
        if r1 <= 0 or r0 >= 200: return
        n = r1 - r0
        step = s32((c1 - c0) * self.recip(n))
        c = s32(c0 << 16)
        if r1 > 200: r1 = 200
        if r0 < 0:
            c = s32(c - s32(r0 * step)); r0 = 0
        for y in range(r0, r1):
            buf[y * 320:(y + 1) * 320] = bytes([(c >> 16) & 0xff]) * 320
            c = s32(c + step)

    # ---- object transform (0x3fb3)
    def transform(self, ob, cam, campos, light, mode, buckets, keep=False):
        M = self.M
        R = [M.i32(ob + 4 * i) for i in range(9)]
        C = [0] * 9
        for r in range(3):
            for c in range(3):
                C[r * 3 + c] = s32(sum(shld_hi(imul64(cam[r * 3 + k], R[k * 3 + c]), 16) for k in range(3)))
        pos = [M.i32(ob + 0x48 + 4 * i) for i in range(3)]
        d = [s32(pos[i] - campos[i]) for i in range(3)]
        T = [s32(sum(s32(cam[r * 3 + k] * d[k]) for k in range(3))) for r in range(3)]
        l = [s32(light[i] - pos[i]) for i in range(3)]
        L = [sar(sum(s32(R[k * 3 + c] * l[k]) for k in range(3)), 16) for c in range(3)]
        ln = math.sqrt(abs(float(L[0]) ** 2 + float(L[1]) ** 2 + float(L[2]) ** 2)) / 200.0
        L = [int(round(v / ln)) if ln else 0 for v in L]   # fistp, round to nearest even
        nv = M.u32(ob + 0x6c); nf = M.u32(ob + 0x70); fp = M.u32(ob + 0x74); vp = M.u32(ob + 0x78)
        V = {}
        self.camv = {}
        for i in range(nv):
            a = vp + 0x24 * i
            v = [M.i32(a), M.i32(a + 4), M.i32(a + 8)]
            z = s32(sum(s32(C[6 + k] * v[k]) for k in range(3)) + T[2])
            sx = sy = None
            if z > 0x1900000:
                f = 0xe00000 // (z >> 16)
                x = s32(sum(s32(C[k] * v[k]) for k in range(3)) + T[0])
                y = s32(sum(s32(C[3 + k] * v[k]) for k in range(3)) + T[1])
                sx = s32(shld_hi(x * f, 5) + 0x1400)
                sy = s32(shld_hi(y * f, 5) + 0xc80)
                self.camv[a] = (x, y, z)
            else:
                self.camv[a] = (0, 0, z)
            V[a] = (z, sx, sy)
        vis = {}
        for i in range(nf):
            a = fp + 0x4c * i
            p = [M.u32(a), M.u32(a + 4), M.u32(a + 8)]
            ok = all(V[q][0] > 0x1900000 for q in p)
            if ok:
                e1x = V[p[1]][1] - V[p[0]][1]; e1y = V[p[1]][2] - V[p[0]][2]
                e2x = V[p[2]][1] - V[p[0]][1]; e2y = V[p[2]][2] - V[p[0]][2]
                ok = s32(e2y * e1x) > s32(e2x * e1y)
            vis[a] = ok
            if ok:
                n = [M.i32(a + 0x24), M.i32(a + 0x28), M.i32(a + 0x2c)]
                sh = sar(sum(s32(n[k] * L[k]) for k in range(3)), 12)
                al = (sh + M.u8(a + 0x1e)) & 0xff
                ah = al >> 5
                if mode == 2: al = (al + M.u8(a + 0x1c)) & 0xff
                if (al >> 5) != ah: al = 0x3a
                M.m[a + 0x1f] = al
                M.m[a + 0x1d] &= 0x7f
            else:
                M.m[a + 0x1d] |= 0x80
        for i in range(nf):
            a = fp + 0x4c * i
            if not vis[a]: continue
            bl = M.u8(a + 0x1d) & 0xf8
            for k in range(3):
                nb = M.u32(a + 0xc + 4 * k)
                if M.u8(nb + 0x1d) & 0x80: bl |= 1 << k
            M.m[a + 0x1d] = bl
            p = [M.u32(a), M.u32(a + 4), M.u32(a + 8)]
            key = sar(sum(sar(V[q][0], 16) for q in p) - 0x4b0, 6)
            key = max(0, min(key, 0x3ff))
            buckets[key].append((a, [V[q] for q in p]))

    def flush(self, buf, buckets, mode):
        M = self.M
        for key in range(1023, -1, -1):
            for a, vs in reversed(buckets[key]):
                pts = [(vs[k][1] >> 5, vs[k][2] >> 5) for k in range(3)]
                self.tri(pts, M.u8(a + 0x1f), buf)
                if mode == 1: continue
                al = M.u8(a + 0x1d)
                if mode == 2:
                    al &= M.u8(a + 0x20)
                    if a == M.u32(0x5047):
                        self.line18b7(buf, M.i32(0x55d6), M.i32(0x55da), M.i32(0x5053), M.i32(0x5057))
                for k, msk in enumerate((9, 0x12, 0x24)):
                    if al & msk:
                        (x0, y0), (x1, y1) = pts[k], pts[(k + 1) % 3]
                        self.line18b7(buf, x0, y0, x1, y1)
            buckets[key] = []

    def scene(self, mode=0):
        """0x5eec into a fresh buffer; returns buffer"""
        M = self.M
        self.oob = 0
        self.rowtab = [[0, 0] for _ in range(201)]
        buf = bytearray(b'\x3a' * 64000)
        campos = [sar(M.i32(0x5df0 + 4 * i), 10) for i in range(3)]
        yaw, pitch = M.i32(0x5de8), M.i32(0x5dec)
        cam = self.b1f(pitch, yaw)
        light = [M.i32(0x1e48c + 4 * i) for i in range(3)]
        s = self.sin[pitch & 0x3ff]; c = self.sin[(pitch + 0x100) & 0x3ff] or 1
        hz = int(s32(s * 0xe0) / c)  # idiv truncates
        hz = -hz + 0x64
        if mode == 0:
            self.grad(buf, hz - 0x15e, hz, 0x80, 0x9f)
            self.grad(buf, hz, hz + 0x578, 0x6d, 0x6d)
            self.line18b7(buf, 0, hz, 0x140, hz)
        self.mark6133()
        objs = [M.u32(0x5db8 + 4 * i) for i in range(12)]
        if M.i32(0x482b) & M32 < 0x600:
            groups = [[0, 1], [2, 3, 4, 5, 6, 7, 8], [9, 10]]
        else:
            groups = [[0, 1], [5, 6, 7], [2], [9, 10], [3, 4], [8]]
        buckets = [[] for _ in range(1024)]
        for g in groups:
            for i in g:
                self.transform(objs[i], cam, campos, light, mode, buckets)
            self.flush(buf, buckets, mode)
        self.cam, self.campos, self.hz = cam, campos, hz
        return buf

    def mark6133(self):
        M = self.M
        n = s32(M.i32(0x1ea95) + M.i32(0x506b)) - 1
        e = 0x5073
        while n > 0:
            f = M.u32(e); b = M.u8(e + 4) & 7
            M.m[f + 0x20] |= b | (b << 3)
            e += 5; n -= 1
        if M.u8(0x506b) == 0:
            M.m[M.u32(e) + 0x20] |= 0x80

    def pencil(self, buf):
        """0x603f after 0x5eec when [0x55d6] < 1000: pencil + shadow + paper combine; returns final buffer"""
        M = self.M
        mode = M.u8(0x1ea94)
        px = sar(s32((M.i32(0x55d6) - 0xa0) * 0x8edb6), 16)
        py = sar(s32((M.i32(0x55da) - 0x64) * 0x8edb6), 16)
        pz = M.i32(0x55de) - (0x32 if M.u8(0x506b) else 0)
        ob = M.u32(0x5de4)
        for i, v in enumerate((px, py, pz)): M.w32(ob + 0x48 + 4 * i, v)
        cam = self.b1f(0, 0)
        campos = [0, 0, -2000]
        a = s32(M.i32(0x55d2) * 2 + 10)
        b = a + 0x5a - sar(M.i32(0x55d6) - 0xa0, 1)
        rot = self.b1f(a, b)
        for i, v in enumerate(rot): M.w32(ob + 4 * i, v)
        light = [M.i32(0x1e48c + 4 * i) for i in range(3)]
        buckets = [[] for _ in range(1024)]
        self.vcache = {}
        self.transform(ob, cam, campos, light, mode, buckets, keep=True)
        # shadow 0x42e4
        tex = bytearray(M.m[M.u32(0x1edf5):M.u32(0x1edf5) + 64000])
        f = 0xe00000 // 2000
        lx, ly, lz = light
        sv = {}
        for va, (x, y, z) in self.camv.items():
            t = int((-lz << 16) / ((z >> 16) - lz + campos[2]))
            X = s32(s32(((x >> 16) - lx + campos[0]) * t) + s32(lx << 16))
            Y = s32(s32(((y >> 16) - ly + campos[1]) * t) + s32(ly << 16))
            sx = ((X * f) >> 32) + 0xa0; sy = ((Y * f) >> 32) + 0x64
            sv[va] = (((sx + 0x8000) & 0xffff) - 0x8000, ((sy + 0x8000) & 0xffff) - 0x8000)
        nf = M.u32(ob + 0x70); fp = M.u32(ob + 0x74)
        for i in range(nf):
            fa = fp + 0x4c * i
            pts = [sv[M.u32(fa + 4 * k)] for k in range(3)]
            self.tri(pts, 0x20, tex, orop=True)
        out = self.combine(buf, None, texbuf=tex)
        self.flush(out, buckets, 1)
        return out

    def combine(self, buf, tex, texbuf=None):
        M = self.M
        T = self.T
        m = M.m
        if texbuf is not None:
            return bytearray(m[T + (texbuf[i] << 8) + buf[i]] for i in range(64000))
        M = self.M
        T = self.T
        m = M.m
        return bytearray(m[T + (m[tex + i] << 8) + buf[i]] for i in range(64000))


# ---------------- per-frame helpers of the 620b loop
def noise_table(R, cl_unused=None):
    """0x726d: 0x800-byte table at 0x69b7 from the 5 (phase, speed, freq, amp) records at 0x71b7"""
    M = R.M
    P = [[M.i32(0x71b7 + 16 * k + 4 * j) for j in range(4)] for k in range(5)]
    out = bytearray(0x800)
    for i in range(0x800):
        x = 0x800 - i
        s = 0
        for ph, sp, fr, am in P:
            idx = (((fr * x + ph) & M32) >> 5) & 0x3ff
            s = s32(s + s32(R.sin[idx] * am))
        v = sar(s, 18) + 0x80
        v = max(0, min(v, 0xfa))
        out[i] = v >> 1
    return out


def curve2ad0(a, m, c, n):
    """0x2ad0: n+1 values of the parabola through (0,a),(1,m),(2,c) sampled at t = i*((65536//n)>>7)/256"""
    step = (65536 // n) >> 7
    B = s32(4 * m - c - 3 * a)
    C = s32(2 * m - 2 * a - B)
    out = []
    t = 0
    for i in range(n + 1):
        v = sar(s32(B * t) + sar(s32(s32(C * t) * t), 8), 8)
        v = sar(v + 2 * a, 1)
        out.append(v)
        t += step
    return out


def warp3b69(nz, u, v):
    """0x3b69: banner surface point for (u, v) using noise table nz"""
    bx = sar(3 * u, 5)
    by = sar(v, 4) + 0x80
    X = sar(s32((nz[bx] - 0x40) * v), 11) + 0x7bc + u + sar(s32((nz[by] - 0x40) * v), 11)
    Y = sar(s32((nz[bx + 0x100] - 0x40) * v), 11) - 0x123 + v + sar(s32((nz[by + 0x100] - 0x40) * v), 11)
    Z = sar(s32((nz[bx + 0x200] - 0x50) * v), 8) - 3 + sar(u, 9) + sar(s32((nz[by + 0x200] - 0x50) * v), 8)
    return X, Y, Z


def banner_vertices(nz):
    sag = curve2ad0(0, 0x5a, 0, 0x11)
    vs = []
    for row in range(11):
        for col in range(17):
            X, Y, Z = warp3b69(nz, col * 0x80, row * 0x5f)
            vs.append((X, Y + sag[col], Z))
    return vs


def camera_eval(R):
    """0x74e8: returns [yaw, pitch, x16.16*?, ...] as stored at 0x5de8"""
    M = R.M
    p = M.u32(0x482b)
    t = p & 0xff
    t2 = t * t
    t3 = (t2 * t) >> 8
    t1 = t << 8
    base = M.u32(0x482f) + (p >> 8) * 0x50
    out = []
    for ch in range(5):
        a, b, c, d = (M.i32(base + 16 * ch + 4 * j) for j in range(4))
        v = s32((d << 16) + s32(a * t3) + s32(b * t2) + s32(c * t1))
        if ch < 2: v = sar(v, 16)
        out.append(v)
    return out


def project7418(R, cam, campos, X, Y, Z):
    dx, dy, dz = X - campos[0], Y - campos[1], Z - campos[2]
    z = s32(cam[6] * dx + cam[7] * dy + cam[8] * dz)
    if z <= 0x1900000: return None
    f = 0xe00000 // (z >> 16)
    x = s32(cam[0] * dx + cam[1] * dy + cam[2] * dz)
    y = s32(cam[3] * dx + cam[4] * dy + cam[5] * dz)
    return s32((x * f) >> 32) + 0xa0, s32((y * f) >> 32) + 0x64


def handwriting(R, buf, nz, cam, campos):
    """0x73a6"""
    M = R.M
    p = M.u32(0x483b); end = M.u32(0x4837)
    P = [0, 0]
    def proj(u, v):
        r = project7418(R, cam, campos, *warp3b69(nz, u, v))
        if r: P[0], P[1] = r
    proj(M.i32(p), M.i32(p + 4)); p += 8
    while True:
        if p >= end: return
        prev = tuple(P)
        u = M.i32(p)
        if u == 0:
            p += 8
            if M.i32(p) != 0:
                proj(M.i32(p), M.i32(p + 4)); p += 8
                continue
            return
        proj(u, M.i32(p + 4)); p += 8
        R.line18b7(buf, prev[0], prev[1], P[0], P[1])


def composite72c4(R, colour, sketch):
    M = R.M
    T = M.u8(0x7324)
    n = M.m[0x69b7:0x69b7 + 0x800]
    out = bytearray(64000)
    for r in range(200):
        ah = (n[0x13f + 200 - r] - 0x12) & 0xff
        o = r * 320
        for x in range(320):
            out[o + x] = sketch[o + x] if ((n[x] + ah) & 0xff) < T else colour[o + x]
    return out


def peel2b6c(R, vga):
    """0x2b6c: draws the page peel straight into vga (bytearray 64000, previous VGA contents)"""
    M = R.M
    S = R.sin
    p = M.i32(0x1eaa9)
    old = M.u32(0x23a0); new = M.u32(0x23a4); paper = M.u32(0x1edf5)
    a = sar(s32(S[(p + 0x100) & 0x3ff] * 0xc8), 16)
    m = sar(s32(S[(sar(p, 1) + 0x100) & 0x3ff] * 0x64), 16)
    A = curve2ad0(a, m, 0, 0x19)
    q = s32(-p)
    a2 = sar(s32(S[q & 0x3ff] * 0x640), 16)
    m2 = sar(s32(S[sar(q, 1) & 0x3ff] * 0x320), 16)
    B = curve2ad0(a2, m2, 0, 0x19)
    ymax, ymin = 0, 0xc8
    srcy = 0xc80000
    mem = M.m
    for i in range(25):
        ecx, eax, edx, ebx = A[i], A[i + 1], B[i], B[i + 1]
        if ecx > ymax: ymax = ecx
        if ecx < ymin: ymin = ecx
        srcy = s32(srcy - 0x80000)
        # 0x2ca2
        src = old
        if eax == ecx: continue
        if eax > ecx:
            src = paper
            ecx, eax = eax, ecx
            edx, ebx = ebx, edx
            if ecx <= 0 or eax >= 0xc8: continue
            if eax < 0: eax = 0
            if ecx > 0xc8: ecx = 0xc8
        else:
            if ecx <= 0 or eax >= ymin: continue
            if eax < 0: eax = 0
            if ecx > ymin: ecx = ymin
        h = ecx - eax
        if h <= 0: continue
        row = eax
        sc = s32((ebx << 5) + 0x10000)
        dsc = s32(s32((edx << 5) + 0x10000) - sc)
        rc = R.recip(h)
        dsc = sar(s32(dsc * rc), 16)
        dy = s32(rc << 3)
        sy = srcy
        for k in range(h & 0xff if h & 0xff else 256):
            e = s32(sc * -160)
            si = src + sar(sy, 16) * 320 + sar(e, 16) + 0xa0
            frac = (e << 16) & M32
            stp_i = sar(sc, 16); stp_f = (sc << 16) & M32
            o = (row + k) * 320
            for x in range(320):
                if 0 <= o + x < 64000:
                    vga[o + x] = mem[si]
                frac += stp_f
                carry = frac >> 32; frac &= M32
                si += stp_i + carry
            sc = s32(sc + dsc); sy = s32(sy + dy)
    if ymax < 0xc8:
        vga[ymax * 320:64000] = mem[new + ymax * 320:new + 64000]
    return vga
