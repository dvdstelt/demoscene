"""Bit-exact Python model of DOPE's 3D engine (documented from p2; the other parts' copies differ in addresses and
a few details, see PARTS below and notes-engine.md).

    frame build = object loop 354a (348b per object: 1d86 matrix, 1c60 vertices, 3377 faces)
                  + radix sort 3585 + draw loop 3310 (triangle filler 2881 / 3030 with span loops 2660 / 2ea0)

Input: a memory dump taken at the start of the frame build (models/engine/run_dope.py --dump pre=CS:<call 354a>).
Output: the render target (320 x 201 bytes, row 0 unused) and the engine's work arrays, to compare with the dump
taken just before the frame copy (--dump drawn=CS:<call 360c>).
"""
import struct

import numpy as np

M32 = 0xffffffff


def s16(x):
    x &= 0xffff
    return x - 0x10000 if x & 0x8000 else x


def s32(x):
    x &= M32
    return x - 0x100000000 if x & 0x80000000 else x


def tdiv(a, b):
    """idiv: quotient truncated toward zero"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def lo(x): return x & 0xffff
def hi(x): return (x >> 16) & 0xffff


# Per-part configuration: addresses in segment 0010 (cs) and the work segment (gs) layout.
#   objs/objlast  object list (0x3a-byte records, slot 0 = camera) and the last record the engine draws
#   sin           2560-word sine table (cos = +512 entries); segenv / segtex: 4-entry segment tables; bump: 256 words
#   attr_c/attr_b global material adders; work: segment of the per-frame arrays (keys, sort temp, per-vertex texture,
#   env, screen, depth, per-face corner pointers A/B/C); target: render target segment (fixed or a cs variable)
#   sortbit       first key bit of the radix sort; texshift: shld count of the texel address; envonly: the filler has
#   the environment-only path (texture segment 0); texsrc: per-vertex texture dword from the mesh or from the
#   object-space normal; envneg: environment coordinates negated (p6); pivot_after: p5 adds the pivot again,
#   unscaled, after the rotation; matrix_round: p2 rounds the matrix products (sar 15 + adc), the others truncate
PARTS = {
    'p2': dict(objs=0x1810, objlast=0x18be, sin=0x410, segenv=0x400, segtex=0x408, bump=0x0000,
               attr_c=0x194f, attr_b=0x194d, work=0x2ccd, keys=0x0000, tmp=0x1450, tex=0x28a0, env=0x3840,
               scr=0x47e0, dep=0x5780, fa=0x6720, fb=0x7148, fc=0x7b70, target=('seg', 0x4527),
               matrix_round=True, nvars=(0x1931, 0x1933), retraces=0x1928, sortbit=18, texshift=9, envonly=True,
               texsrc='mesh', envneg=False, pivot_after=None),
    'p3': dict(objs=0x1810, objlast=0x1932, sin=0x410, segenv=0x400, segtex=0x408, bump=0x0000,
               attr_c=0x19be, attr_b=0x19bc, work=0x343d, keys=0x0000, tmp=0x0fa0, tex=0x1f40, env=0x2ee0,
               scr=0x3e80, dep=0x4e20, fa=0x5dc0, fb=0x6590, fc=0x6d60, target=('var', 0x19d0),
               matrix_round=False, nvars=(0x19a2, 0x19a4), retraces=0x199a, sortbit=21, texshift=8, envonly=False,
               texsrc='normal', envneg=False, pivot_after=None),
    'p5': dict(objs=0x1412, objlast=0x14c0, sin=0x012, segenv=0x000, segtex=0x008, bump=0x375b,
               attr_c=0x1550, attr_b=0x154e, work=0x2685, keys=0x0000, tmp=0x1450, tex=0x28a0, env=0x3840,
               scr=0x47e0, dep=0x5780, fa=0x6720, fb=0x7148, fc=0x7b70, target=('var', 0x1564),
               matrix_round=False, nvars=(0x1530, 0x1532), retraces=0x1528, sortbit=21, texshift=10, envonly=True,
               texsrc='mesh', envneg=False, pivot_after=True),
    'p6': dict(objs=0x1620, objlast=0x16ce, sin=0x220, segenv=0x210, segtex=0x218, bump=0x0010,
               attr_c=0x1762, attr_b=0x1760, work=0x667b, keys=0x3200, tmp=0x41a0, tex=0x0000, env=0x0c80,
               scr=0x1900, dep=0x2580, fa=0x5140, fb=0x5910, fc=0x60e0, target=('var', 0x177a),
               matrix_round=False, nvars=(0x173e, 0x1740), retraces=0x1736, sortbit=20, texshift=10, envonly=True,
               texsrc='mesh', envneg=True, pivot_after=None),
    'p7': dict(objs=0x1810, objlast=0x1932, sin=0x410, segenv=0x400, segtex=0x408, bump=0x0000,
               attr_c=0x19c0, attr_b=0x19be, work=0x3de1, keys=0x0000, tmp=0x1f40, tex=0x3e80, env=0x55f0,
               scr=0x6d60, dep=0x84d0, fa=0x9c40, fb=0xabe0, fc=0xbb80, target=('var', 0x19d4),
               matrix_round=False, nvars=(0x19a2, 0x19a4), retraces=0x199a, sortbit=21, texshift=8, envonly=True,
               texsrc='normal', envneg=False, pivot_after=None),
}


def sext15(n):
    """(n << 1) as 16 bits, then sar 1: n with bit 14 copied into bit 15 (p3/p5/p7 vertex rotate)"""
    return s16((n << 1) & 0xffff) >> 1


class Engine:
    def __init__(self, dump, part='p2', draw_dump=None):
        """dump: state at the start of the frame build; draw_dump: state just before the draw loop (maps, tables)"""
        self.d = dump
        self.dd = dd = draw_dump or dump
        self.c = PARTS[part]
        c = self.c
        self.sin = [dump.s16(0x10, c['sin'] + 2 * i) for i in range(2560)]
        self.segenv = [dd.seg_of(dd.cs16(c['segenv'] + 2 * i)) for i in range(4)]
        self.segtex = [dd.cs16(c['segtex'] + 2 * i) for i in range(4)]
        self.segtex = [dd.seg_of(s) if s else 0 for s in self.segtex]
        self.bump = [dd.cs16(c['bump'] + 2 * i) for i in range(256)]
        n = 0x10000
        self.W = {k: None for k in ('tex', 'env', 'scr', 'dep')}
        self.vtex = {}      # global vertex byte offset (4*index) -> dword
        self.venv = {}
        self.vscr = {}      # -> (x, y)
        self.vdep = {}
        self.faces = []     # (key dword, A, B, C) in slot order
        tseg = c['target'][1] if c['target'][0] == 'seg' else dd.seg_of(dd.cs16(c['target'][1]))
        self.target_seg = tseg
        self.buf = bytearray(n)

    # ---- 1d86: rotation matrix from three 16-bit angles (65536 = 360 degrees), Q15 entries --------------------
    def matrix(self, a1, a2, a3):
        S = lambda a: self.sin[(a & 0xffe) // 2]           # sin table at cs:0410 (2048 entries a turn)
        C = lambda a: self.sin[(a & 0xffe) // 2 + 512]     # cos = sin + 512 entries (cs:0810)
        sa, ca, sb, cb, sc, cc = S(a1), C(a1), S(a2), C(a2), S(a3), C(a3)
        if self.c['matrix_round']:
            r = lambda x: (s32(x) >> 15) + ((s32(x) >> 14) & 1)     # sar 15 + adc 0: round half up
        else:
            r = lambda x: s32(x) >> 15
        t = lambda x: s32(x) >> 15                                   # sar without adc (one place in p2)
        m = [0] * 9
        m[0] = r(cb * cc - r(sb * sa) * sc)
        m[1] = r(ca * sc)
        m[2] = r(sb * cc + r(cb * sa) * sc)
        m[3] = r(-(cb * sc + t(sb * sa) * cc))
        m[4] = r(ca * cc)
        m[5] = r(-(sb * sc - r(cb * sa) * cc))
        m[6] = r(-sb * ca)
        m[7] = -sa
        m[8] = r(cb * ca)
        return [s32(x) for x in m]

    # ---- 348b + 1c60 + 3377 for one object ------------------------------------------------------------------
    def object(self, rec, cam, vbase, slot):
        d = self.d
        c = self.c
        seg = d.seg_of(struct.unpack_from('<H', rec, 0)[0])
        f = struct.unpack_from('<h', rec, 4)[0]
        pos = struct.unpack_from('<3i', rec, 6)
        ang = struct.unpack_from('<3H', rec, 0x1e)
        attr = struct.unpack_from('<H', rec, 0x2a)[0]
        cull = struct.unpack_from('<h', rec, 0x2c)[0]
        piv = struct.unpack_from('<3i', rec, 0x2e)
        cpos = struct.unpack_from('<3i', cam, 6)
        cang = struct.unpack_from('<3H', cam, 0x1e)
        a = [s16(ang[i] + cang[i]) >> 4 for i in range(3)]     # (obj + camera angle) sar 4 -> [1d80..84]
        T = [s32(pos[i] + cpos[i]) for i in range(3)]          # [1910..18]
        m = self.matrix(*[x & 0xffff for x in a])
        self.last_m = m
        nv, nf, opos, ofac, onrm, otex = (d.u16(seg, 2 * i) for i in range(6))
        if nf == 0:
            return vbase, slot
        for i in range(nv):
            g = 4 * (vbase + i)
            nx, ny, nz = (d.s16(seg, onrm + 6 * i + 2 * k) for k in range(3))
            ex = s32(m[0] * nx + m[1] * ny + m[2] * nz)
            ey = s32(m[3] * nx + m[4] * ny + m[5] * nz)
            e = (ex & 0xffff0000) | ((ey >> 16) & 0xffff)
            if c['envneg']:
                e = -e & M32                                   # p6: neg edx
            self.venv[g] = e + 0x40004000 & M32
            if c['texsrc'] == 'mesh':
                self.vtex[g] = d.u32(seg, otex + 4 * i)
            else:
                self.vtex[g] = ((sext15(ny) & 0xffff) << 16) | (sext15(nx) & 0xffff)
            x, y, z = (d.s16(seg, opos + 6 * i + 2 * k) + piv[k] for k in range(3))
            px = s32(m[0] * x + m[1] * y + m[2] * z)
            py = s32(m[3] * x + m[4] * y + m[5] * z)
            pz = s32(m[6] * x + m[7] * y + m[8] * z)
            if c['pivot_after']:
                px, py, pz = s32(px + piv[0]), s32(py + piv[1]), s32(pz + piv[2])
            px = s32(px + (T[0] << 8))
            py = s32(py + (T[1] << 8))
            self.vdep[g] = s16(((s32((pz >> 16) * f)) >> 14) + 0x4000)
            den = s32(pz - (T[2] << 8))
            q = tdiv(px * f, den)
            sx = s16((q >> 3) + ((q >> 2) & 1) + 0xa0)
            q = s32(tdiv(py * f, den) * 3)
            sy = s16((q >> 5) + ((q >> 4) & 1) + 0x64)
            self.vscr[g] = (sx, sy)
        ac = d.u16(0x10, c['attr_c'])
        ab = d.u16(0x10, c['attr_b'])
        for j in range(nf):
            w, A, B, C = (d.u16(seg, ofac + 8 * j + 2 * k) for k in range(4))
            A, B, C = A + 4 * vbase, B + 4 * vbase, C + 4 * vbase
            (xa, ya), (xb, yb), (xc, yc) = self.vscr[A], self.vscr[B], self.vscr[C]
            cx = s16((xc - xa) * (yb - ya))
            ax = s16(s16((xb - xa) * (yc - ya)) - cx)
            if (cull == 0 and ax < 0) or (cull != 0 and ax >= 0):
                continue
            key = (self.vdep[A] + self.vdep[B] + self.vdep[C]) & 0xffff
            bsel = ((w >> 8) + (attr >> 8) + ab) & 3
            csel = ((w & 0xff) + (attr & 0xff) + ac) & 3
            self.faces.append(((key << 16) | slot, A, B | bsel, C | csel))
            slot += 1
        return vbase + nv, slot

    def build(self):
        d = self.d
        c = self.c
        cam = d.b(0x10, c['objs'], 0x3a)
        vbase = slot = 0
        o = c['objs']
        while True:
            rec = d.b(0x10, o, 0x3a)
            w0 = struct.unpack_from('<H', rec, 0)[0]
            if w0 == 0:
                break
            if w0 != 0xffff and o <= c['objlast'] and struct.unpack_from('<H', rec, 2)[0] == 0:
                vbase, slot = self.object(rec, cam, vbase, slot)
            o += 0x3a
        self.nvert, self.nface = vbase, slot

    # ---- 3585: LSD radix sort on bits 18..31 of the key, stable --------------------------------------------
    def sort(self):
        keys = [f[0] for f in self.faces]
        order = list(range(len(keys)))
        for bit in range(self.c['sortbit'], 32):
            zero = [i for i in order if not keys[i] >> bit & 1]
            one = [i for i in order if keys[i] >> bit & 1]
            order = zero + one
        self.order = order
        return [keys[i] for i in order]

    # ---- 3310 + fillers ----------------------------------------------------------------------------------------
    def draw(self):
        for i in self.order:
            key, A, B, C = self.faces[i]
            self.tri(A & ~3, B & ~3, C & ~3, self.segtex[B & 3], self.segenv[C & 3])

    def tri(self, A, B, C, tseg, eseg):
        P = lambda g: ((self.vscr[g][0] & 0xffff) << 16) | (self.vscr[g][1] & 0xffff)
        va, vb, vc = P(A), P(B), P(C)
        out = sum(1 for v in (va, vb, vc) if v > 0x1400000)
        if out == 3:
            return
        clip = out != 0
        y = lambda v: s16(v)
        if not y(va) < y(vb):
            va, vb, A, B = vb, va, B, A
        if not y(vb) < y(vc):
            vb, vc, B, C = vc, vb, C, B
        if not y(va) < y(vb):
            va, vb, A, B = vb, va, B, A
        if y(va) > 0xc9 or y(vc) < 1:
            return
        if y(va) == y(vb) and not s32(va) < s32(vb):
            va, vb, A, B = vb, va, B, A
        self.st = st = {}
        st['A'], st['B'], st['C'] = va, vb, vc
        eA, eB, eC = self.venv[A], self.venv[B], self.venv[C]
        tA, tB, tC = self.vtex[A], self.vtex[B], self.vtex[C]
        if tseg or not self.c['envonly']:
            self.tri_bump(va, vb, vc, eA, eB, eC, tA, tB, tC, tseg, eseg, clip)
        else:
            self.tri_env(va, vb, vc, eA, eB, eC, eseg, clip)

    # common: edge slopes
    @staticmethod
    def slopes(va, vb, vc):
        dy1, dy2, dy3 = lo(vb - va), lo(vc - va), lo(vc - vb)
        s1 = tdiv(s32(vb - va), dy1) if dy1 else None
        s2 = tdiv(s32(vc - va), dy2) if dy2 else None
        s3 = tdiv(s32(vc - vb), dy3) if dy3 else None
        return dy1, dy2, dy3, s1, s2, s3

    @staticmethod
    def wdiv(a, b):
        """cwd; idiv bx (16-bit) -> 16-bit quotient"""
        return lo(tdiv(s16(a), s16(b)))

    @staticmethod
    def hgrad(a, b, c, dy1, dy2, ebp):
        """2a68: per-pixel step of one 16-bit coordinate: (b - (a + (c - a) * dy1 / dy2)) << 16 / width"""
        q = tdiv(s16(a - c) * s16(dy1), s16(dy2))
        ax = lo(q - a + b)
        return lo(tdiv(s32(ax << 16), s32(ebp)))

    def tri_bump(self, va, vb, vc, eA, eB, eC, tA, tB, tC, tseg, eseg, clip):
        dy1, dy2, dy3, s1, s2, s3 = self.slopes(va, vb, vc)
        if dy2 == 0:
            return
        flat = dy1 == 0
        L, R = va, (va + 0x7fff) & M32
        cenv, ctex = eA, tA                        # [2650], [264c]
        g = self.g = {}
        if flat or s1 == s2:
            ebp = s32(vb - va)
            if ebp == 0:
                return
            lstep = (self.g_prev_env, self.g_prev_tex) if hasattr(self, 'g_prev_env') else (0, 0)
        else:
            ebp = s32((s1 - s2) * dy1)
            if s1 >= s2:   # B right of the long edge: left edge A->C
                lstep = ((self.wdiv(hi(eC) - hi(eA), dy2) << 16) | self.wdiv(lo(eC) - lo(eA), dy2),
                         (self.wdiv(hi(tC) - hi(tA), dy2) << 16) | self.wdiv(lo(tC) - lo(tA), dy2))
            else:
                lstep = ((self.wdiv(hi(eB) - hi(eA), dy1) << 16) | self.wdiv(lo(eB) - lo(eA), dy1),
                         (self.wdiv(hi(tB) - hi(tA), dy1) << 16) | self.wdiv(lo(tB) - lo(tA), dy1))
        self.g_prev_env, self.g_prev_tex = lstep
        pe = (self.hgrad(hi(eA), hi(eB), hi(eC), dy1, dy2, ebp) << 16) | self.hgrad(lo(eA), lo(eB), lo(eC), dy1, dy2, ebp)
        pt = (self.hgrad(hi(tA), hi(tB), hi(tC), dy1, dy2, ebp) << 16) | self.hgrad(lo(tA), lo(tB), lo(tC), dy1, dy2, ebp)
        ya, yb, yc = s16(va), s16(vb), s16(vc)
        if yb <= 1 or flat:
            cenv = (cenv + dy1 * lstep[0]) & M32
            ctex = (ctex + dy1 * lstep[1]) & M32
            e = s32(s2 * dy1 + va)
            b = s32(vb)
            if e >= b:
                e, b = b, e
            L, R = e & M32, (b + 0x7fff) & M32
            row = max(ya, 1)
            L = (L + ((row * 5) << 22)) & M32
            R = (R + ((row * 5) << 22)) & M32
        else:
            ls, rs = (s1, s2) if s1 < s2 else (s2, s1)
            if yb > 0xc9:
                yb = 0xc9
            if ya < 1:
                n = 1 - ya
                cenv = (cenv + n * lstep[0]) & M32
                ctex = (ctex + n * lstep[1]) & M32
                L = (L + n * ls) & M32
                R = (R + n * rs) & M32
                ya = 1
            if yb == ya:
                return
            L = (L + ((ya * 320) << 16)) & M32
            R = (R + ((ya * 320) << 16)) & M32
            L, R, cenv, ctex = self.spans_bump(L, R, ls, rs, ya, yb - ya, cenv, ctex, lstep, pe, pt, tseg, eseg, clip)
        if dy3 == 0:
            return
        lstep = ((self.wdiv(hi(eC) - hi(cenv), dy3) << 16) | self.wdiv(lo(eC) - lo(cenv), dy3),
                 (self.wdiv(hi(tC) - hi(ctex), dy3) << 16) | self.wdiv(lo(tC) - lo(ctex), dy3))
        self.g_prev_env, self.g_prev_tex = lstep
        ls, rs = (s2, s3) if s2 > s3 else (s3, s2)
        yc = 0xc9 if lo(vc) > 0xc9 else s16(vc)
        if yb < 1:
            n = 1 - yb
            cenv = (cenv + n * lstep[0]) & M32
            ctex = (ctex + n * lstep[1]) & M32
            L = (L + n * ls) & M32
            R = (R + n * rs) & M32
            yb = 1
        if yc == yb:
            return
        self.spans_bump(L, R, ls, rs, yb, yc - yb, cenv, ctex, lstep, pe, pt, tseg, eseg, clip)

    @staticmethod
    def pack_edges(L, R, ls, rs, add_row=True):
        if add_row:
            ls = (ls + 0x1400000) & M32
            rs = (rs + 0x1400000) & M32
        imm1 = (lo(rs) << 16) | hi(ls)
        imm2 = (lo(ls) << 16) | hi(rs)
        ebp = (lo(R) << 16) | hi(L)
        edx = (lo(L) << 16) | hi(R)
        return ebp, edx, imm1, imm2

    @staticmethod
    def step_edges(ebp, edx, imm1, imm2):
        s = ebp + imm1
        ebp, c = s & M32, s >> 32
        s = edx + imm2 + c
        edx, c = s & M32, s >> 32
        ebp = (ebp + c) & M32
        return ebp, edx

    def spans_bump(self, L, R, ls, rs, y, n, cenv, ctex, lstep, pe, pt, tseg, eseg, clip):
        d = self.dd
        mem = d.mem
        tb = d.lin(tseg)
        eb = d.lin(eseg)
        k = self.c['texshift']
        bump = self.bump
        buf = self.buf
        ebp, edx, imm1, imm2 = self.pack_edges(L, R, ls, rs)
        rowbase = y * 320
        for _ in range(n):
            l, r = ebp & 0xffff, edx & 0xffff
            ecx, ed = ctex, cenv
            cnt = 0
            if clip:
                if l < rowbase:
                    nl = s32(l - rowbase)      # negative
                    ecx = (ecx - nl * pt) & M32
                    ed = (ed - nl * pe) & M32
                    l = rowbase
                if l < rowbase + 320:          # else the span starts right of the screen: skip the line
                    if r < rowbase:
                        r = rowbase
                    if r >= rowbase + 320:
                        r = rowbase + 320
                    cnt = s16(r - l)
            else:
                cnt = s16(r - l)
            if cnt > 0:
                di = l
                for _ in range(cnt):
                    bx = ((((ecx >> 8) & 0xff) << k) | (ecx >> (32 - k))) & 0x7f7f
                    t = mem[tb + bx]
                    bx = ((((ed >> 8) & 0xff) << 8) | (ed >> 24)) + bump[t] & 0xffff
                    buf[di & 0xffff] = mem[eb + bx]
                    di += 1
                    ecx = (ecx + pt) & M32
                    ed = (ed + pe) & M32
            cenv = (cenv + lstep[0]) & M32
            ctex = (ctex + lstep[1]) & M32
            ebp, edx = self.step_edges(ebp, edx, imm1, imm2)
            rowbase += 320
        L = ((ebp & 0xffff) << 16) | (edx >> 16)
        R = ((edx & 0xffff) << 16) | (ebp >> 16)
        return L, R, cenv, ctex

    def tri_env(self, va, vb, vc, eA, eB, eC, eseg, clip):
        dy1, dy2, dy3, s1, s2, s3 = self.slopes(va, vb, vc)
        if dy2 == 0:
            return
        flat = dy1 == 0
        ecx = eA
        L, R = va, (va + 0x8000) & M32
        if flat or s1 == s2:
            ebp = s32(vb - va)
            if ebp == 0:
                return
            lstep = getattr(self, 'g_prev_env2', 0)
        else:
            ebp = s32((s1 - s2) * dy1)
            if s1 >= s2:
                lstep = (self.wdiv(hi(eC) - hi(eA), dy2) << 16) | self.wdiv(lo(eC) - lo(eA), dy2)
            else:
                lstep = (self.wdiv(hi(eB) - hi(eA), dy1) << 16) | self.wdiv(lo(eB) - lo(eA), dy1)
        self.g_prev_env2 = lstep
        pe = (lo(self.hgrad(hi(eA), hi(eB), hi(eC), dy1, dy2, ebp) - 1) << 16) | \
            lo(self.hgrad(lo(eA), lo(eB), lo(eC), dy1, dy2, ebp) - 1)
        ya, yb, yc = s16(va), s16(vb), s16(vc)
        if yb <= 1 or flat:
            ecx = (ecx + dy1 * lstep) & M32
            e = s32(s2 * dy1 + va)
            b = s32(vb)
            if e >= b:
                e, b = b, e
            L, R = e & M32, (b + 0x7fff) & M32
            row = max(ya, 1)
            L = (L + ((row * 5) << 22)) & M32
            R = (R + ((row * 5) << 22)) & M32
        else:
            ls, rs = (s1, s2) if s1 < s2 else (s2, s1)
            if yb > 0xc9:
                yb = 0xc9
            if ya < 1:
                n = 1 - ya
                ecx = (ecx + ((lo(n * hi(lstep)) << 16) | lo(n * lo(lstep)))) & M32
                L = (L + n * ls) & M32
                R = (R + n * rs) & M32
                ya = 1
            if yb == ya:
                return
            L = (L + ((ya * 320) << 16)) & M32
            R = (R + ((ya * 320) << 16)) & M32
            L, R, ecx = self.spans_env(L, R, ls, rs, ya, yb - ya, ecx, lstep, pe, eseg, clip)
        if dy3 == 0:
            return
        lstep = (self.wdiv(hi(eC) - hi(ecx), dy3) << 16) | self.wdiv(lo(eC) - lo(ecx), dy3)
        self.g_prev_env2 = lstep
        ls, rs = (s2, s3) if s2 > s3 else (s3, s2)
        yc = 0xc9 if lo(vc) > 0xc9 else s16(vc)
        if yb < 1:
            n = 1 - yb
            ecx = (ecx + ((lo(n * hi(lstep)) << 16) | lo(n * lo(lstep)))) & M32
            L = (L + n * ls) & M32
            R = (R + n * rs) & M32
            yb = 1
        if yc == yb:
            return
        self.spans_env(L, R, ls, rs, yb, yc - yb, ecx, lstep, pe, eseg, clip)

    def spans_env(self, L, R, ls, rs, y, n, ecx, lstep, pe, eseg, clip):
        mem = self.dd.mem
        eb = self.dd.lin(eseg)
        buf = self.buf
        ebp, edx, imm1, imm2 = self.pack_edges(L, R, ls, rs)
        rowbase = y * 320
        cur = ecx
        for _ in range(n):
            l, r = ebp & 0xffff, edx & 0xffff
            e = cur
            cnt = 0
            if clip:
                if l < rowbase:
                    k = s32(l - rowbase)
                    e = (e - k * pe) & M32
                    l = rowbase
                if l < rowbase + 320:
                    if r < rowbase:
                        r = rowbase
                    if r >= rowbase + 320:
                        r = rowbase + 319     # sic: the clipped env filler never draws column 319
                    cnt = s16(r - l)
            else:
                cnt = s16(r - l)
            if cnt > 0:
                cnt &= 0xff
                if cnt == 0:
                    cnt = 256
                di = l
                for _ in range(cnt):
                    bx = (((e >> 8) & 0xff) << 8) | (e >> 24)
                    buf[di & 0xffff] = mem[eb + bx]
                    di += 1
                    e = (e + pe) & M32
            cur = (cur + lstep) & M32
            ebp, edx = self.step_edges(ebp, edx, imm1, imm2)
            rowbase += 320
        L = ((ebp & 0xffff) << 16) | (edx >> 16)
        R = ((edx & 0xffff) << 16) | (ebp >> 16)
        return L, R, cur
