"""Model of CLONE's 3D engine: vertex normals 0x416b61, back-face test and depth buckets 0x416929, the draw 0x4169f8
(shade per vertex), the frustum clipper 0x41674b / 0x4166dc / 0x416669 / 0x416472 (0x416297 per plane) and Craft's
texture mapper 0x415127 (edges 0x414f2f / 0x4150f7, bilinear filter, log-space shade table), with the x87's
truncating arithmetic (fpu.py).

usage: python3 engine.py [--smooth] DUMP.npz [...]   (dumps from run_engine.py --dump-draws: memory at the entry of
                                                     0x4169f8 and the target page before / after)
For each dump: rebuilds the normals, cull flags and bucket lists from the transformed vertices and compares them
with the dump; draws the buckets into the 'before' page and compares the result with 'after' pixel by pixel (the
modelled page is saved next to the dump as *_model.npy for capture_check.py). Also counts pixels drawn by a face
that is not the nearest at that pixel (painter's order vs a depth buffer). --smooth also draws the closed form
(notes-engine.md section 10) and reports how far it is from the original.
"""
import struct
import sys

import numpy as np

import fpu
from fpu import L, f32, magic_low, s32

BASE = 0x400000
M52 = L(6755399441055744.0)            # 1.5 * 2^52 (float at 0x422578)
M60 = L(1729382256910270464.0)         # 1.5 * 2^60 (float at 0x422588)
BUCKETS = 0xf5d3dc                     # 4096 face lists, drawn from 0xfff down to 0


class Mem:
    def __init__(self, mem):
        self.m = mem

    def u32(self, a):
        return struct.unpack_from('<I', self.m, a - BASE)[0]

    def i32(self, a):
        return struct.unpack_from('<i', self.m, a - BASE)[0]

    def f(self, a):
        return L(np.frombuffer(self.m[a - BASE:a - BASE + 4].tobytes(), '<f4')[0])

    def d(self, a):
        return L(np.frombuffer(self.m[a - BASE:a - BASE + 8].tobytes(), '<f8')[0])

    def byte(self, a):
        return int(self.m[a - BASE])


# ---------------------------------------------------------------------------------------------------------------
# 0x416b61: vertex normals = normalised sum of the (unnormalised) face normals (p0 - p1) x (p2 - p1)

def vertex_normals(pos, faces, nv):
    """pos: dict address -> (x, y, z) extended; faces: list of (pos addr a, b, c, normal addr a, b, c).
    Returns dict normal address -> (x, y, z) as stored (floats)."""
    n = {}
    for fa in faces:
        for na in fa[3:6]:
            n[na] = [L(0), L(0), L(0)]
    for p0a, p1a, p2a, n0, n1, n2 in faces:
        p0, p1, p2 = pos[p0a], pos[p1a], pos[p2a]
        ay, bz = p0[1] - p1[1], p2[2] - p1[2]
        ay_f, bz_f = f32(ay), f32(bz)
        az, bx = p0[2] - p1[2], p2[0] - p1[0]
        ax, by = p0[0] - p1[0], p2[1] - p1[1]
        nx = f32(ay * bz_f - az * by)
        ny = az * bx - ax * bz_f
        nz = ax * by - bx * ay_f
        for na in (n0, n1, n2):
            v = n[na]
            n[na] = [f32(v[0] + nx), f32(v[1] + ny), f32(v[2] + nz)]
    out = {}
    for na, (x, y, z) in n.items():
        ln = np.sqrt(x * x + y * y + z * z)
        r = L(1) / ln
        out[na] = (f32(x * r), f32(y * r), f32(z * r))
    return out


# ---------------------------------------------------------------------------------------------------------------
# 0x416929: back-face test (v0 . (v1 x v2) < 0 -> hidden, flag at face +0x34) and depth buckets

def cull_and_bucket(fdata, fz02):
    """fdata: list of (face address, v0, v1, v2) with camera-space positions. Returns (flags, buckets dict)."""
    flags, buckets, extra = {}, {}, {'key<0': 0, 'key>=4096': 0, 'key_max': -1}
    for fa, v0, v1, v2 in fdata:
        st1 = v0[0] * (v1[1] * v2[2] - v1[2] * v2[1]) - v0[1] * (v1[0] * v2[2] - v1[2] * v2[0])
        st0 = (v1[1] * v2[0] - v1[0] * v2[1]) * v0[2]
        hidden = st0 > st1
        flags[fa] = int(hidden)
        if hidden:
            continue
        k = fpu.fist((v0[2] + v1[2] + v2[2]) * fz02)
        if k < 0:
            extra['key<0'] += 1
            k = 0
        elif k >= 0x1000:
            extra['key>=4096'] += 1
            continue
        extra['key_max'] = max(extra['key_max'], k)
        buckets.setdefault(k, []).insert(0, fa)           # pushed at the head of the list
    return flags, buckets, extra


# ---------------------------------------------------------------------------------------------------------------
# frustum clipping: 0x416297 clips (field0 - field2 < 0 is inside); the four stages transform the vertices in
# between (negate field 0, swap fields 0 and 1, negate field 0) so the same routine clips x < z, -x < z, y < z, -y < z.
# A vertex is [f0, f1, z, sx, sy, u, v, shade] (offsets 0, 4, 8, 0xc, 0x10, 0x14, 0x18, 0x1c).

LERP_FIELDS = (0, 1, 2, 5, 6, 7)


def clip(a, b, c):
    d = [f32(v[0] - v[2]) for v in (a, b, c)]
    code = int(np.signbit(np.float32(d[0]))) | int(np.signbit(np.float32(d[1]))) << 1 | \
        int(np.signbit(np.float32(d[2]))) << 2
    if code == 0:
        return []
    if code == 7:
        return [(a, b, c)]
    if code in (1, 6):                                   # a is the odd vertex
        esi, edi, edx = a, b, c
        t1, t2 = d[0] / (d[0] - d[1]), d[0] / (d[0] - d[2])
    elif code in (2, 5):                                 # b
        esi, edi, edx = b, a, c
        t1, t2 = d[1] / (d[1] - d[0]), d[1] / (d[1] - d[2])
    else:                                                # c (codes 3, 4)
        esi, edi, edx = c, b, a
        t1, t2 = d[2] / (d[2] - d[1]), d[2] / (d[2] - d[0])
    o1, o2 = L(1) - t1, L(1) - t2
    A, B = [L(0)] * 8, [L(0)] * 8
    for k in LERP_FIELDS:
        A[k] = f32(edi[k] * t1 + esi[k] * o1)
        B[k] = f32(edx[k] * t2 + esi[k] * o2)
    if code in (1, 2, 4):                                # one vertex inside: one triangle
        return [(esi, A, B)]
    return [(A, edi, edx), (A, B, edx)]                  # two inside: a quad as two triangles


def neg0(v):
    w = list(v)
    w[0] = -w[0]
    return w


def swap01(v):
    w = list(v)
    w[0], w[1] = w[1], w[0]
    return w


def clip_frustum(a, b, c):
    """0x41674b: all three z negative -> nothing; then the four planes. Yields the triangles the filler gets,
    with fields 0 / 1 = (-y, -x) of the camera space."""
    if np.signbit(np.float32(a[2])) and np.signbit(np.float32(b[2])) and np.signbit(np.float32(c[2])):
        return
    for t1 in clip(a, b, c):                                              # x < z
        for t2 in clip(*[neg0(v) for v in t1]):                            # -x < z
            for t3 in clip(*[swap01(v) for v in t2]):                      # y < z
                for t4 in clip(*[neg0(v) for v in t3]):                    # -y < z
                    yield t4


# ---------------------------------------------------------------------------------------------------------------
# Craft's texture mapper 0x415127

class Mapper:
    def __init__(self, mem, page):
        self.mem = mem
        self.page = page                                  # uint32 array of 320 * 200 (the target)
        self.A = mem.f(0x422534)                          # column scale (-0.625)
        self.B = mem.f(0x422538)                          # row scale (-0.3125 or -0.390625)
        self.MX = mem.d(0x422564)                         # 1.5*2^52 + 160*65536
        self.MY = mem.d(0x42256c)                         # 1.5*2^52 + 100*65536
        self.k16 = mem.f(0x422584)                        # 2^24
        self.c65536 = mem.f(0x422580)
        self.inv65536 = mem.f(0x42257c)
        self.eight = mem.f(0x4225ac)
        self.ftab = [mem.f(0x42258c + 4 * i) for i in range(8)]          # 0..7
        self.rtab = [mem.f(0x4225b0 + 4 * i) for i in range(8)]          # 0, 1, 1/2 .. 1/7
        self.shade_tab = np.frombuffer(mem.m[0x426b8c - BASE:0x426b8c - BASE + 0x1400].tobytes(), np.uint8)
        w = np.frombuffer(mem.m[0x426d8c - BASE:0x426d8c - BASE + 4096].tobytes(), '<f4')
        self.w = w.reshape(16, 16, 4).astype(np.int64)                   # [fu][fv][w00, w10, w01, w11]
        self.texcache = {}
        self.smooth = False
        self.zlast = np.zeros(64000)                      # 1/z of the face that drew each pixel last
        self.znear = np.zeros(64000)                      # largest 1/z drawn at each pixel (the nearest)
        self.stats = {'tri': 0, 'pixels': 0, 'shade_over_511': 0, 'degenerate': 0, 'left_clip': 0}

    def texture(self, addr):
        t = self.texcache.get(addr)
        if t is None:
            n = 256 * 257 + 1
            t = np.frombuffer(self.mem.m[addr - BASE:addr - BASE + 4 * n].tobytes(), '<u4').astype(np.int64)
            self.texcache[addr] = t
        return t

    def project(self, v):
        z = v[2]
        k = self.k16 / z
        sx = s32(magic_low(k * v[1] * self.A, self.MX))  # column, 16.16
        sy = s32(magic_low(k * v[0] * self.B, self.MY))  # row, 16.16
        inv = L(1) / z
        return [None, None, f32(inv), sx, sy, f32(v[5] * inv), f32(v[6] * inv), v[7]]

    def edge_pos(self, start, delta, p):                 # 0x4150f7
        slope = L(delta[3]) / L(delta[4]) * self.c65536
        return {'slope_i': fpu.fist(slope), 'x': fpu.fist(slope * p + L(start[3]))}

    def edge_attr(self, start, delta, p, g):             # 0x414f2f
        slope = L(delta[3]) / L(delta[4]) * self.c65536
        e = {'slope_i': fpu.fist(slope)}
        si = e['slope_i'] >> 16
        x = fpu.fist(slope * p + L(start[3]))
        e['x'] = s32(x)
        q = f32((f32(slope) * p - L(x & 0xffff)) * self.inv65536)
        for name, fld in (('u', 5), ('v', 6), ('z', 2)):
            ax, ay = g[name + 'x'], g[name + 'y']
            nc = L(si) * ax + ay
            e[name + '_nc'] = f32(nc)
            e[name + '_c'] = f32(nc + ax)
            e[name] = f32(start[fld] + p * ay + q * ax + ax)
        sx, sy = g['sx'], g['sy']
        nc = L(si) * sx + sy
        e['s_nc'] = magic_low(nc, M52)
        e['s_c'] = magic_low(nc + sx, M52)
        e['s'] = (magic_low(start[7] + p * sy + q * sx, M52) + g['sx_i']) & 0xffffffff
        return e

    def triangle(self, v0, v1, v2, tex):
        """v0, v1, v2 = the filler's buffers 0x427e44, 0x427e64, 0x427e84 (esi = v2, edi = v1, ebx = v0)."""
        self.stats['tri'] += 1
        esi, edi, ebx = self.project(v2), self.project(v1), self.project(v0)
        if esi[4] > edi[4]:
            esi, edi = edi, esi
        if edi[4] > ebx[4]:
            edi, ebx = ebx, edi
            if esi[4] > edi[4]:
                esi, edi = edi, esi
        top, mid, bot = esi, edi, ebx
        row0 = top[4] >> 16

        def delta(a, b):                                 # b - a: ints for sx, sy, floats for z, u, v, shade
            return [None, None, f32(b[2] - a[2]), s32(b[3] - a[3]), s32(b[4] - a[4]),
                    f32(b[5] - a[5]), f32(b[6] - a[6]), f32(b[7] - a[7])]
        db, de = delta(top, bot), delta(top, mid)
        D = L(de[3]) * L(db[4]) - L(de[4]) * L(db[3])
        if D == 0:
            self.stats['degenerate'] += 1
            return
        r = self.c65536 / D
        bdy, bdx, edy, edx = L(db[4]) * r, L(db[3]) * r, L(de[4]) * r, L(de[3]) * r
        g = {}
        for name, fld in (('u', 5), ('v', 6), ('z', 2), ('s', 7)):
            gx = de[fld] * bdy - db[fld] * edy
            gy = db[fld] * edx - de[fld] * bdx
            g[name + 'x'], g[name + 'y'] = f32(gx), f32(gy)
            if name == 's':
                g['sx_i'] = fpu.fist(gx)
        g['sx'], g['sy'] = g.pop('sx'), g.pop('sy')
        g8 = {n: f32(g[n + 'x'] * self.eight) for n in ('u', 'v', 'z')}
        p_top = f32(L((~top[4]) & 0xffff) * self.inv65536)
        p_mid = f32(L((~mid[4]) & 0xffff) * self.inv65536)
        neg = bool(np.signbit(np.float32(D)))
        state = {'row': row0, 'ptr_row': row0}
        if not neg:
            if db[4] == 0:
                return
            left = self.edge_attr(top, db, p_top, g)
            if de[4] != 0:
                right = self.edge_pos(top, de, p_top)
                self.rows(left, right, g, g8, tex, state, top[4], mid[4])
            second = lambda: self.edge_pos(mid, delta(mid, bot), p_mid)
        else:
            if db[4] == 0:
                return
            right = self.edge_pos(top, db, p_top)
            if de[4] != 0:
                left = self.edge_attr(top, de, p_top, g)
                self.rows(left, right, g, g8, tex, state, top[4], mid[4])
            second = lambda: self.edge_attr(mid, delta(mid, bot), p_mid, g)
        dmb = delta(mid, bot)
        if dmb[4] == 0:
            return
        if not neg:
            right = second()
        else:
            left = second()
        self.rows(left, right, g, g8, tex, state, mid[4], bot[4])

    def rows(self, le, re_, g, g8, tex, state, ya, yb):
        row = ya >> 16
        end = min(yb >> 16, 200)
        n = end - row
        ptr = state['ptr_row']
        while n > 0:
            if row >= 0:
                if self.smooth:
                    self.span_smooth(le, re_, g, tex, ptr)
                else:
                    self.span(le, re_, g, g8, tex, ptr)
            # step the edges (0x4161d5)
            x = (le['x'] + le['slope_i']) & 0xffffffff
            carry = (x & 0xffff) < (le['slope_i'] & 0xffff)
            le['x'] = s32(x)
            re_['x'] = s32(re_['x'] + re_['slope_i'])
            sfx = '_c' if carry else '_nc'
            for name in ('u', 'v', 'z'):
                le[name] = f32(le[name] + le[name + sfx])
            le['s'] = (le['s'] + le['s' + sfx]) & 0xffffffff
            ptr += 1
            row += 1
            n -= 1
        state['ptr_row'] = ptr

    def span(self, le, re_, g, g8, tex, prow):
        xr = min(re_['x'] >> 16, 320)
        xl = le['x'] >> 16
        if xl < 0:
            self.stats['left_clip'] += 1                  # the original computes clipped values and ignores them
            xl = 0
        n = xr - xl
        if n <= 0:
            return
        e = (n - 1) & 7                                   # first block: e + 1 pixels, then 8-pixel blocks
        blocks = (n - 1) >> 3
        z0, u0p, v0p = le['z'], le['u'], le['v']
        inv0 = L(1) / z0
        ze = f32(g['zx'] * self.ftab[e] + z0)
        ue = f32(g['ux'] * self.ftab[e] + u0p)
        ve = f32(g['vx'] * self.ftab[e] + v0p)
        u0, v0 = u0p * inv0, v0p * inv0
        u0f, v0f = f32(u0), f32(v0)
        U0, V0 = magic_low(u0, M60), magic_low(v0, M60)
        inve = L(1) / ze
        uE, vE = ue * inve, ve * inve
        Ue, Ve = magic_low(uE, M60), magic_low(vE, M60)
        dU = magic_low((uE - u0f) * self.rtab[e], M60)
        dV = magic_low((vE - v0f) * self.rtab[e], M60)
        ue, ve, ze = f32(ue + g8['u']), f32(ve + g8['v']), f32(ze + g8['z'])
        step = ((dU << 16) | (dV & 0xffff)) & 0xffffffff
        uv = ((U0 << 16) | (V0 & 0xffff)) & 0xffffffff
        uvs = []
        for k in range(e + 1):
            uvs.append(uv)
            uv = (uv + step) & 0xffffffff
        for b in range(blocks):                           # 0x415918: perspective every 8 pixels
            inv = L(1) / ze
            U, V = magic_low(ue * inv, M60), magic_low(ve * inv, M60)
            ue, ve, ze = f32(ue + g8['u']), f32(ve + g8['v']), f32(ze + g8['z'])
            dU, dV = s32(U - Ue) >> 3, s32(V - Ve) >> 3
            step = ((dU << 16) | (dV & 0xffff)) & 0xffffffff
            uv = ((((Ue << 16) | (Ve & 0xffff)) & 0xffffffff) + step) & 0xffffffff
            Ue, Ve = U, V
            for k in range(8):
                uvs.append(uv)
                uv = (uv + step) & 0xffffffff
        uvs = np.array(uvs, np.int64)
        shade = (le['s'] + g['sx_i'] * np.arange(n, dtype=np.int64)) & 0xffffffff
        self.page[prow * 320 + xl: prow * 320 + xl + n] = self.pixels(uvs, shade, tex)
        self.stats['pixels'] += n
        # painter's order vs a depth buffer: 1/z at the sample point of every pixel
        zq = np.array([float(z0) + k * float(g['zx']) for k in range(n)])
        o = prow * 320 + xl
        self.zlast[o:o + n] = zq
        self.znear[o:o + n] = np.maximum(self.znear[o:o + n], zq)

    def span_smooth(self, le, re_, g, tex, prow):
        """The same span evaluated the way a GPU would (for the notes: how far the closed form is from the
        original): u / z per pixel, bilinear with exact fractions and wrapping, shade not floored, the
        exponential instead of the table. Same sample points and coverage as the original."""
        xr = min(re_['x'] >> 16, 320)
        xl = max(le['x'] >> 16, 0)
        n = xr - xl
        if n <= 0:
            return
        k = np.arange(n, dtype=np.float64)
        zq = float(le['z']) + k * float(g['zx'])
        u = (float(le['u']) + k * float(g['ux'])) / zq / 65536.0      # texels (u is u * 2^24)
        v = (float(le['v']) + k * float(g['vx'])) / zq / 65536.0
        s = (le['s'] + g['sx_i'] * k) / 65536.0
        t = self.texture(tex)[:256 * 256].reshape(256, 256)
        U0, V0 = np.floor(u), np.floor(v)
        fu, fv = u - U0, v - V0
        U0, V0 = U0.astype(np.int64) & 255, V0.astype(np.int64) & 255
        U1, V1 = (U0 + 1) & 255, (V0 + 1) & 255
        out = np.zeros(n, np.int64)
        for bit in (0, 8, 16):
            c = lambda vv, uu: ((t[vv, uu] >> bit) & 255).astype(np.float64)
            lv = ((1 - fu) * (1 - fv) * c(V0, U0) + fu * (1 - fv) * c(V0, U1) + (1 - fu) * fv * c(V1, U0) +
                  fu * fv * c(V1, U1))
            out |= np.minimum(255, 128 * np.exp(0.011 * (s + lv - 256))).astype(np.int64) << bit
        self.page[prow * 320 + xl: prow * 320 + xr] = out.astype(np.uint32)

    def pixels(self, uv, shade, tex):
        """bilinear 2x2 texel average with 4-bit weights (0x426d8c), per channel floor(sum / 256), then the
        shade table: channel = T[(shade >> 16) + channel] (0x426b8c, T[i] = min(255, 128 exp(0.011 (i - 256))))."""
        t = self.texture(tex)
        U, V = uv >> 24, (uv >> 8) & 0xff
        fu, fv = (uv >> 20) & 0xf, (uv >> 4) & 0xf
        i = V * 256 + U
        c00, c10, c01, c11 = t[i], t[i + 1], t[i + 256], t[i + 257]
        w = self.w[fu, fv]
        out = np.zeros(len(uv), np.int64)
        sh = shade >> 16
        over = np.zeros(len(uv), bool)
        for bit in (0, 8, 16):
            s = (w[:, 0] * ((c00 >> bit) & 255) + w[:, 1] * ((c10 >> bit) & 255) +
                 w[:, 2] * ((c01 >> bit) & 255) + w[:, 3] * ((c11 >> bit) & 255)) >> 8
            over |= sh + s >= 512                         # past the table: bytes of the weight table 0x426d8c
            self.stats['max_index'] = max(self.stats.get('max_index', 0), int((sh + s).max()))
            out |= self.shade_tab[sh + s].astype(np.int64) << bit
        self.stats['shade_over_511'] += int(over.sum())
        self.stats['max_shade'] = max(self.stats.get('max_shade', 0), int(sh.max()))
        return out.astype(np.uint32)


# ---------------------------------------------------------------------------------------------------------------
# 0x4169f8: draw the bucket lists, far to near

def draw(mem, page, smooth=False):
    mapper = Mapper(mem, page)
    mapper.smooth = smooth
    scale = mem.f(0x427f70)
    K = mem.d(0x41a4c8)                                   # 2437939.2 = 37.2 * 65536
    c65536, c256 = mem.f(0x41a4c0), mem.f(0x41a4c4)
    order = []
    for b in range(0xfff, -1, -1):
        f = mem.u32(BUCKETS + 4 * b)
        while f:
            order.append(f)
            verts = []
            for i in range(3):
                p = mem.u32(f + 4 + 4 * i)
                u = f32(mem.f(f + 0x1c + 8 * i) * c65536 * c256)
                v = f32(mem.f(f + 0x20 + 8 * i) * c65536 * c256)
                verts.append([mem.f(p), mem.f(p + 4), mem.f(p + 8), None, None, u, v, None])
            if mem.u32(f + 0x10):
                for i in range(3):
                    c = L(1) + -mem.f(mem.u32(f + 0x10 + 4 * i) + 8)
                    verts[i][7] = f32(c * (c * c) * K * scale)
            else:
                for i in range(3):
                    verts[i][7] = f32(L(mem.i32(f + 0x3c + 4 * i)) * scale)
            tex = mem.u32(f + 0x38)
            for t in clip_frustum(*verts):
                mapper.triangle(t[0], t[1], t[2], tex)
            f = mem.u32(f)
    drawn = mapper.zlast > 0
    mapper.stats['not_nearest'] = int((drawn & (mapper.zlast < mapper.znear * (1 - 1e-3))).sum())
    return mapper.stats, order


OBJECTS = {7: (0xf613dc, 0xf613e0), 8: (0xf72d4c, 0xf72d50),   # part: (face array pointer, face count)
           9: (0xf73584, 0x428018), 10: (0xf73908, 0x428154)}


def check_object(mem, part):
    """Recompute the cull flags and the bucket lists from the transformed vertices (and for the geospheres of
    parts 7 / 8 the vertex normals) and compare them with the dump."""
    fp, cp = OBJECTS[part]
    faces, nf = mem.u32(fp), mem.u32(cp)
    fl = [tuple(mem.u32(faces + 0x48 * k + o) for o in (4, 8, 0xc, 0x10, 0x14, 0x18)) for k in range(nf)]
    pos = {}
    for f in fl:
        for a in f[:3]:
            pos[a] = (mem.f(a), mem.f(a + 4), mem.f(a + 8))
    res = {}
    if part in (7, 8):
        n = vertex_normals(pos, fl, 0)
        res['normals'] = len(n)
        res['normals_differ'] = sum(1 for a, v in n.items()
                                    if any(np.float32(v[k]) != np.float32(mem.f(a + 4 * k)) for k in range(3)))
    flags, buckets, extra = cull_and_bucket([(faces + 0x48 * k, pos[fl[k][0]], pos[fl[k][1]], pos[fl[k][2]])
                                             for k in range(nf)], mem.d(0x41a4b8))
    res['faces'] = nf
    res['cull_flags_differ'] = sum(1 for fa, h in flags.items() if h != mem.u32(fa + 0x34))
    dumped = {}
    for b in range(0x1000):
        f, lst = mem.u32(BUCKETS + 4 * b), []
        while f:
            lst.append(f)
            f = mem.u32(f)
        if lst:
            dumped[b] = lst
    res['buckets_identical'] = dumped == buckets
    res['in_buckets'] = sum(len(v) for v in dumped.values())
    res.update(extra)
    return res


def main():
    fpu.chop()
    flags = [a for a in sys.argv[1:] if a.startswith('--')]
    for path in [a for a in sys.argv[1:] if not a.startswith('--')]:
        d = np.load(path)
        mem = Mem(d['mem'])
        part = int(d['part'])
        print('%s: part %d, tick %d, scale [0x427f70] = %g, projection %g %g' % (
            path.rsplit('/', 1)[-1], part, int(d['tick']), float(mem.f(0x427f70)), float(mem.f(0x422534)),
            float(mem.f(0x422538))))
        if part in OBJECTS:
            print('  sort:', check_object(mem, part))
        page = d['before'].view('<u4').copy()
        stats, order = draw(mem, page)
        after = d['after'].view('<u4')
        diff = page != after
        print('  draw: %d faces, %d triangles, %d pixels; %d of 64000 pixels differ (%d changed by the draw); %s' % (
            len(order), stats['tri'], stats['pixels'], diff.sum(), (after != d['before'].view('<u4')).sum(),
            {k: v for k, v in stats.items() if k not in ('tri', 'pixels') and v}))
        if diff.any():
            ys, xs = np.nonzero(diff.reshape(200, 320))
            print('  first differences at', list(zip(xs[:8].tolist(), ys[:8].tolist())))
        np.save(path.replace('.npz', '_model.npy'), page)
        if '--smooth' in flags:                           # the closed form, for comparison
            sp = d['before'].view('<u4').copy()
            draw(mem, sp, smooth=True)
            drawn = after != d['before'].view('<u4')
            a = np.stack([(after >> b) & 255 for b in (16, 8, 0)], -1)[drawn].astype(int)
            b = np.stack([(sp >> b) & 255 for b in (16, 8, 0)], -1)[drawn].astype(int)
            print('  closed form (per-pixel perspective, exact bilinear, exp instead of the table): mean |diff| '
                  '%.2f, 99th percentile %d, max %d (0..255, over the %d drawn pixels)' % (
                      np.abs(a - b).mean(), np.percentile(np.abs(a - b), 99), np.abs(a - b).max(), drawn.sum()))


if __name__ == '__main__':
    main()
