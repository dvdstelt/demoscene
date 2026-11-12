"""Integer model of the p3 / p5 engine path used for the Phong / env / bump objects: matrix (p3 1df6), vertex
transform and projection (1bb0 + 1cd0), back-face test and depth key (2887), radix sort (2a95), triangle set-up
(2211) and the span fillers (1ff0 unclipped, 20e0 clipped), with the exact 16.16 / packed 32-bit arithmetic.

render(part, cam, obj, mesh, maps, bumptab, fb) draws one object into fb (64320+ bytes, offset y*320+x, row y = screen
row y-1) exactly like the engine (verified against engine frame buffers, see verify_p3.py / verify_p5.py).
"""
import struct
import numpy as np

M32 = 0xffffffff


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= M32
    return v - 0x100000000 if v & 0x80000000 else v


def tdiv(a, b):
    """x86 idiv: truncate toward zero"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


class Mesh:
    def __init__(self, img, seg):
        b = seg * 16
        self.vc, self.fc, vo, fo, no, xo = struct.unpack_from('<6H', img, b)
        self.V = np.frombuffer(img[b + vo:b + vo + 6 * self.vc], '<i2').reshape(-1, 3).astype(np.int64)
        self.F = np.frombuffer(img[b + fo:b + fo + 8 * self.fc], '<u2').reshape(-1, 4).astype(np.int64)
        self.N = np.frombuffer(img[b + no:b + no + 6 * self.vc], '<i2').reshape(-1, 3).astype(np.int64)
        self.UV = np.frombuffer(img[b + xo:b + xo + 4 * self.vc], '<u4').astype(np.int64)


def sine_table(img, off):
    """the engine's 2560-word table built by p3 19f0 / p5 1584 from the 512-word quarter wave at `off`"""
    q = list(struct.unpack_from('<512h', img, off))
    t = [0] * 2560
    t[0:512] = q
    for k in range(512):                    # copied backwards from word 1023 (0xc0e - 0x410 = 0x7fe bytes)
        t[1023 - k] = q[k]
    for k in range(1024):                   # negated copy of words 0..1023 to 1024..2047 (sequential, reads new data)
        t[1024 + k] = -t[k] if k < 1024 else 0
    # note: the 0x400-word negated copy reads words 0..1023 while writing 1024..2047: no overlap
    t[2048:2560] = t[0:512]
    return t


def matrix(sin, ax, ay, az):
    def sc(a):
        k = ((s16(a) >> 4) & 0xffe) >> 1
        return sin[k], sin[k + 512]
    s1, c1 = sc(ax); s2, c2 = sc(ay); s3, c3 = sc(az)
    m00 = (c2 * c3 - ((s2 * s1) >> 15) * s3) >> 15
    m01 = (c1 * s3) >> 15
    m02 = (s2 * c3 + ((c2 * s1) >> 15) * s3) >> 15
    m10 = (-(c2 * s3 + ((s2 * s1) >> 15) * c3)) >> 15
    m11 = (c1 * c3) >> 15
    m12 = (-(s2 * s3 - ((c2 * s1) >> 15) * c3)) >> 15
    m20 = (-s2 * c1) >> 15
    m21 = -s1
    m22 = (c2 * c1) >> 15
    # the code wraps each 32-bit product sum; the values here stay far inside 32 bits
    return [[m00, m01, m02], [m10, m11, m12], [m20, m21, m22]]


def transform(mesh, M, pos, pivot, zoom, attrA_kind):
    """per vertex: screen dword (sx<<16 | sy&0xffff), depth word, attribute A (bump coords), attribute B (env)"""
    out = []
    px, py, pz = pos
    for i in range(mesh.vc):
        nx, ny, nz = (int(v) for v in mesh.N[i])
        # attribute B: rotated normal x (high word) and y (low word), + 0x40004000
        e0 = s32(M[0][0] * nx + M[0][1] * ny + M[0][2] * nz)
        e1 = s32(M[1][0] * nx + M[1][1] * ny + M[1][2] * nz)
        B = ((e0 & 0xffff0000) | ((e1 & M32) >> 16)) + 0x40004000 & M32
        if attrA_kind == 'normal':          # p3: object-space normal x, y sign-extended from 15 bits
            ax = s16(nx << 1) >> 1 & 0xffff
            ay = s16(ny << 1) >> 1 & 0xffff
            A = (ay << 16) | ax
        else:                               # p5: the mesh's UV dword
            A = int(mesh.UV[i])
        x, y, z = (int(v) for v in mesh.V[i])
        x += pivot[0]; y += pivot[1]; z += pivot[2]
        X = s32(M[0][0] * x + M[0][1] * y + M[0][2] * z)
        Y = s32(M[1][0] * x + M[1][1] * y + M[1][2] * z)
        Z = s32(M[2][0] * x + M[2][1] * y + M[2][2] * z)
        X = s32(X + (px << 8)); Y = s32(Y + (py << 8))
        depth = (((Z >> 16) * zoom) >> 14) + 0x4000 & 0xffff
        ebp = s32(Z - s32(pz << 8))
        q = tdiv(X * zoom, ebp)
        sx = (q >> 3) + ((q >> 2) & 1) + 0xa0
        q = tdiv(Y * zoom, ebp)
        q3 = s32(q * 3)
        sy = (q3 >> 5) + ((q3 >> 4) & 1) + 0x64
        out.append(((sx & 0xffff) << 16 | (sy & 0xffff), depth, A, B))
    return out


def visible_faces(mesh, verts, cull):
    """2887: back-face test with 16-bit products, depth key = sum of the vertex depth words; then 2a95's sort"""
    lst = []
    for f in range(mesh.fc):
        mat, ia, ib, ic = (int(v) for v in mesh.F[f])
        a, b, c = verts[ia >> 2], verts[ib >> 2], verts[ic >> 2]
        sxa, sya = s16(a[0] >> 16), s16(a[0]); sxb, syb = s16(b[0] >> 16), s16(b[0]); sxc, syc = s16(c[0] >> 16), s16(c[0])
        cx = s16(s16(sxc - sxa) * s16(syb - sya))
        ax = s16(s16(sxb - sxa) * s16(syc - sya))
        d = s16(ax - cx)
        if (cull == 0 and d < 0) or (cull != 0 and d >= 0):
            continue
        key = (a[1] + b[1] + c[1]) & 0xffff
        lst.append((key, f))
    # binary LSD radix sort on bits 21..31 of (key<<16 | index) = key bits 5..15, stable
    lst.sort(key=lambda kf: kf[0] >> 5)
    return [f for _, f in lst]


class Filler:
    """2211 + 1ff0 / 20e0 (p3) and 1d51 + 1b30 / 1c20 (p5 bump) / 2500 + 2370 / 2420 (p5 env)"""

    def __init__(self, img, fb, pixel):
        self.img = img
        self.fb = fb
        self.pixel = pixel      # function(A, B) -> colour index, per pixel

    def tri(self, va, vb, vc):
        P = [va, vb, vc]
        S = [p[0] for p in P]
        # all three to the right of x=320 (unsigned compare with 320<<16): skip
        n_out = sum(1 for s in S if s > 0x1400000)
        if n_out == 3:
            return
        clip = n_out != 0
        # sort by sy (signed 16-bit low word)
        def sy(i): return s16(P[i][0])
        idx = [0, 1, 2]
        if not sy(idx[0]) < sy(idx[1]): idx[0], idx[1] = idx[1], idx[0]
        if not sy(idx[1]) < sy(idx[2]): idx[1], idx[2] = idx[2], idx[1]
        if not sy(idx[0]) < sy(idx[1]): idx[0], idx[1] = idx[1], idx[0]
        a, b, c = (P[i] for i in idx)
        if s16(a[0]) > 0xc9 or s16(c[0]) < 1:
            return
        if s16(a[0]) == s16(b[0]) and s32(a[0]) >= s32(b[0]):
            a, b = b, a
        da, db, dc = s32(a[0]), s32(b[0]), s32(c[0])
        Ba, Bb, Bc = a[3], b[3], c[3]
        Aa, Ab, Ac = a[2], b[2], c[2]
        LB = Ba; LA = Aa                      # [1fe8], [1fe4]
        flat_top = True
        dy_ab = s16(db - da)
        if dy_ab != 0:
            s_ab = tdiv(s32(db - da), dy_ab); flat_top = False
        else:
            s_ab = 0
        dy_ac = s16(dc - da)
        if dy_ac == 0:
            return
        s_ac = tdiv(s32(dc - da), dy_ac)
        dy_bc = s16(dc - db)
        s_bc = tdiv(s32(dc - db), dy_bc) if dy_bc != 0 else 0
        XL = da & M32; XR = (da + 0x7fff) & M32           # [27e8], [27ec]
        def w(v, k): return s16(v >> (16 * k))
        def step16(x, y, n): return tdiv(s16(y - x), n) & 0xffff
        ebp = s32((s_ab - s_ac) * (dy_ab & 0xffff))
        if s_ab == s_ac or dy_ab == 0:
            ebp = s32(db - da)
            if ebp == 0:
                return
            sB = sA = 0                        # left-edge steps stay as they were ([2096].. stale): see notes
            stB = stA = None
        elif s_ab >= s_ac:                     # b right of a->c: left edge a->c
            stB = (step16(w(Ba, 1), w(Bc, 1), dy_ac) << 16) | step16(w(Ba, 0), w(Bc, 0), dy_ac)
            stA = (step16(w(Aa, 1), w(Ac, 1), dy_ac) << 16) | step16(w(Aa, 0), w(Ac, 0), dy_ac)
        else:                                  # left edge a->b
            stB = (step16(w(Ba, 1), w(Bb, 1), dy_ab) << 16) | step16(w(Ba, 0), w(Bb, 0), dy_ab)
            stA = (step16(w(Aa, 1), w(Ab, 1), dy_ab) << 16) | step16(w(Aa, 0), w(Ab, 0), dy_ab)
        if stB is None:
            stB = getattr(self, 'stB', 0); stA = getattr(self, 'stA', 0)   # stale from the previous face
        # horizontal gradients (23ef..2478)
        def hgrad(xa, xb, xc):
            q = tdiv(s16(xa - xc) * dy_ab, dy_ac)
            q = s16(q - xa + xb)
            return tdiv(s32(q << 16), ebp) & 0xffff
        hB = (hgrad(w(Ba, 1), w(Bb, 1), w(Bc, 1)) << 16) | hgrad(w(Ba, 0), w(Bb, 0), w(Bc, 0))
        hA = (hgrad(w(Aa, 1), w(Ab, 1), w(Ac, 1)) << 16) | hgrad(w(Aa, 0), w(Ab, 0), w(Ac, 0))
        self.hA, self.hB = hA, hB
        ya, yb, yc = s16(da), s16(db), s16(dc)
        if yb <= 1 or flat_top:
            # skip the top half
            t = dy_ab & 0xffff
            edi = s32(s_ac * t) + 0
            LB = (LB + t * stB) & M32; LA = (LA + t * stA) & M32
            edi = s32(edi + XL)
            esi = db
            if s32(edi) >= s32(esi):
                edi, esi = esi, edi
            XL = edi & M32; XR = (esi + 0x7fff) & M32
            row = max(ya, 1)
            XL = (XL + (row * 320 << 16)) & M32; XR = (XR + (row * 320 << 16)) & M32
            self.stB, self.stA = stB, stA
        else:
            sl, sr = (s_ab, s_ac) if s_ab < s_ac else (s_ac, s_ab)
            yb_c = min(yb, 0xc9)
            if ya < 1:
                k = 1 - ya
                LB = (LB + k * stB) & M32; LA = (LA + k * stA) & M32
                XL = (XL + k * sl) & M32; XR = (XR + k * sr) & M32
                ya = 1
            n = yb_c - ya
            if n == 0:
                return
            XL = (XL + (ya * 320 << 16)) & M32; XR = (XR + (ya * 320 << 16)) & M32
            XL, XR, LA, LB = self.rows(ya, n, sl, sr, XL, XR, LA, LB, stA, stB, clip)
            self.stB, self.stA = stB, stA
        # bottom half (257b..)
        if dy_bc == 0:
            return
        stB = (step16(w(LB, 1), w(Bc, 1), dy_bc) << 16) | step16(w(LB, 0), w(Bc, 0), dy_bc)
        stA = (step16(w(LA, 1), w(Ac, 1), dy_bc) << 16) | step16(w(LA, 0), w(Ac, 0), dy_bc)
        self.stB, self.stA = stB, stA
        sl, sr = (s_ac, s_bc) if s_ac > s_bc else (s_bc, s_ac)
        yc_c = min(yc, 0xc9)
        yb2 = yb
        if yb2 < 1:
            k = 1 - yb2
            LB = (LB + k * stB) & M32; LA = (LA + k * stA) & M32
            XL = (XL + k * sl) & M32; XR = (XR + k * sr) & M32
            yb2 = 1
        n = yc_c - yb2
        if n == 0:
            return
        self.rows(yb2, n, sl, sr, XL, XR, LA, LB, stA, stB, clip)

    def rows(self, y0, n, sl, sr, XL, XR, LA, LB, stA, stB, clip):
        """1ff0 / 20e0: n rows from row y0; XL/XR hold (address << 16 | fraction) in 16.16"""
        fb = self.fb
        sl2 = (sl + 0x1400000) & M32; sr2 = (sr + 0x1400000) & M32
        base = y0 * 320
        hA, hB = self.hA, self.hB
        for r in range(n):
            left = XL >> 16; right = XR >> 16
            if not clip:
                cnt = right - left
                if s16(left - right) < 0:
                    self.span(left, cnt, LA, LB, hA, hB)
            else:
                A0, B0 = LA, LB
                l = left
                if l < base:
                    k = l - base
                    A0 = (A0 - k * hA) & M32; B0 = (B0 - k * hB) & M32
                    l = base
                    ok = True
                else:
                    ok = l < base + 320
                if ok:
                    rr = min(max(right, base), base + 320)
                    cnt = rr - l
                    if cnt > 0:
                        self.span(l, cnt, A0, B0, hA, hB)
                base += 320
            # x accumulators: exact packed adc trick
            ebp = ((XR & 0xffff) << 16) | (XL >> 16)        # low: left int, high: right frac
            edx = ((XL & 0xffff) << 16) | (XR >> 16)        # low: right int, high: left frac
            esi = ((sr2 & 0xffff) << 16) | (sl2 >> 16)
            edi = ((sl2 & 0xffff) << 16) | (sr2 >> 16)
            t = ebp + esi; cf = t >> 32; ebp = t & M32
            t = edx + edi + cf; cf = t >> 32; edx = t & M32
            ebp = (ebp + cf) & M32
            XL = ((ebp & 0xffff) << 16) | (edx >> 16)
            XR = ((edx & 0xffff) << 16) | (ebp >> 16)
            LB = (LB + stB) & M32; LA = (LA + stA) & M32
        return XL, XR, LA, LB

    def span(self, x0, cnt, A0, B0, hA, hB):
        k = np.arange(cnt, dtype=np.uint64)
        A = (np.uint64(A0) + k * np.uint64(hA)) & np.uint64(M32)
        B = (np.uint64(B0) + k * np.uint64(hB)) & np.uint64(M32)
        cols = self.pixel(A, B, cnt)
        end = x0 + cnt
        if end > len(self.fb):
            cols = cols[:len(self.fb) - x0]; end = len(self.fb)
        self.fb[x0:end] = cols
