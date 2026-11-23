"""A bit-exact Python model of Reve's 3D engine (REVE.EXE, LCA's code): notes-engine.md describes it.

  World(wld)            the world loader 0x21d440 / 0x21d538 / 0x21d9ac: objects, faces, vertices, face and vertex
                        normals, bounding-box corners
  Anim(ani)             the ANI stream and the per-refresh step 0x25f3ae (camera, object positions and angles)
  Renderer              the renderer 0x25f5b5: camera matrix 0x21db9c, object setup 0x21ddf8 / 0x21dfb8 (matrices,
                        culling), the object sort, vertex transform 0x21e54c, near clipping 0x21e864, back faces,
                        the face sort 0x21e6ec, lighting 0x21e72c / 0x21e7c8 and the rasterizers 0x21bccc (flat
                        n-gon 0x21bd4c, texture 0x21c6b4, texture + Gouraud 0x21cc28) with the screen clippers
                        0x21b14c / 0x21b42c / 0x21b70c / 0x21b9ec; PART4's bounding-box wireframe 0x25f26e
  aa_pass(src, dst, T)  the full-screen pass 0x2045fa (ANTIALIA.PAL); fin_pass(new, old, T) is 0x2046c7

Everything is integer arithmetic with the original's widths (16-bit words, 32-bit registers, 64-bit products).
"""
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
IMG_BASE = 0x20427c
_IMG = None


def img():
    global _IMG
    if _IMG is None:
        _IMG = open(os.path.join(RE, 'unpacked', 'reve_img.bin'), 'rb').read()
    return _IMG


def img_rd(a, n):
    return img()[a - IMG_BASE:a - IMG_BASE + n]


def arch_file(name):
    return open(os.path.join(RE, 'unpacked', 'files', name.replace('\\', '/')), 'rb').read()


# ---------------------------------------------------------------- integer helpers
def s16(x):
    x &= 0xffff
    return x - 0x10000 if x & 0x8000 else x


def s32(x):
    x &= 0xffffffff
    return x - 0x100000000 if x & 0x80000000 else x


def s8(x):
    x &= 0xff
    return x - 0x100 if x & 0x80 else x


def idiv(a, b):
    """x86 idiv: quotient truncated toward zero."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def mul15(p, q):
    """imul word; shld dx, ax, 1 (or shrd ax, dx, 15): bits 15..30 of the signed product, as a word."""
    return s16((p * q) >> 15)


# sine table 0x2176c8: 5120 words, 4096 per turn (cosine = +1024 entries); angles index it with (a & 0x1ffe)
SIN = list(struct.unpack('<5120h', img_rd(0x2176c8, 10240)))


def S(a): return SIN[(a & 0x1ffe) >> 1]


def C(a): return SIN[((a & 0x1ffe) >> 1) + 1024]


def isqrt_table(i):
    """The byte table 0x219ec8 (filled by 0x2175bc): isqrt(i) for i < 1156; beyond it holds other data (not
    reached for distances below 34816 / 17408)."""
    if i < 1156:
        return math.isqrt(i)
    raise ValueError('sqrt table index %d beyond the filled part' % i)


def isqrt32(x):
    """0x2175dc: integer square root of an unsigned 32-bit value (floor; checked against the routine)."""
    return math.isqrt(x & 0xffffffff)


# ---------------------------------------------------------------- the world (WLD)
class Vertex:
    __slots__ = ('x', 'y', 'z', 'u', 'v', 'nx', 'ny', 'nz', 'xc', 'yc', 'zc', 'zc32', 'sx', 'sy', 'shade', 'clip')

    def __init__(self, x, y, z, u, v):
        self.x, self.y, self.z, self.u, self.v = x, y, z, u, v
        self.nx = self.ny = self.nz = 0
        self.shade = 0xffff
        self.clip = 0
        self.xc = self.yc = self.zc = self.sx = self.sy = 0
        self.zc32 = 0

    def copy(self):
        n = Vertex.__new__(Vertex)
        for k in Vertex.__slots__:
            setattr(n, k, getattr(self, k))
        return n


class Face:
    __slots__ = ('type', 'colour', 'flat', 'tex', 'a', 'b', 'c', 'nx', 'ny', 'nz', 'nearclip')

    def __init__(self, t, colour, tex, a, b, c):
        self.type, self.colour, self.tex = t, colour, tex
        self.flat = 0
        self.a, self.b, self.c = a, b, c
        self.nx = self.ny = self.nz = 0


class Object:
    def __init__(self):
        self.pos = [0, 0, 0]          # +4 / +8 / +0xc (integer world position, refreshed from pos16 each render)
        self.pos16 = [0, 0, 0]        # +0x1c / +0x20 / +0x24: position << 16 (the ANI adds 16.16 deltas)
        self.ang = [0, 0, 0]          # +0x10 / +0x14 / +0x18: angles << 16 (8192 per turn after >> 16)
        self.P = [0, 0, 0]            # +0x28 / +0x2c / +0x30
        self.R = [0] * 9              # +0x34..0x54
        self.Cm = [0] * 9             # +0x58..0x78
        self.corners = []             # +0x7c: 8 x 0x16 bytes: [x, y, z, xc, yc, zc, sx, sy]


def _clamp16(q):
    return 0x7fff if q > 0x7fff else (-0x8000 if q < -0x8000 else q)


class World:
    """0x21d440: the WLD file. byte 1, then 5 dwords: total vertices, total faces, objects, 0, 0; per object
    (0x21d538): byte 0, dwords vertices, faces, x, y, z; faces of 9 bytes (type, colour, texture, 3 vertex words);
    vertices of 10 bytes (x, y, z, u, v words)."""

    def __init__(self, data):
        assert data[0] == 1
        self.nv, self.nf, self.no = struct.unpack_from('<3i', data, 1)
        p = 0x15
        self.objects = []
        for k in range(self.no):
            o = Object()
            assert data[p] == 0
            nv, nf, x, y, z = struct.unpack_from('<5i', data, p + 1)
            o.pos = [x, y, z]
            o.pos16 = [s32(x << 16), s32(y << 16), s32(z << 16)]
            p += 0x15
            fr = []
            for i in range(nf):
                t, col, tex = data[p], data[p + 1], data[p + 2]
                a, b, c = struct.unpack_from('<3H', data, p + 3)
                fr.append((t, col, tex, a, b, c))
                p += 9
            o.verts = []
            for i in range(nv):
                o.verts.append(Vertex(*struct.unpack_from('<5h', data, p)))
                p += 10
            o.faces = [Face(t, col, tex, o.verts[a], o.verts[b], o.verts[c]) for t, col, tex, a, b, c in fr]
            self._normals(o)
            self._corners(o)
            self.objects.append(o)

    @staticmethod
    def _normals(o):
        acc = {id(v): [0, 0, 0] for v in o.verts}         # dwords +0x14 (x), +0xa (y), +0xe (z)
        for f in o.faces:
            a, b, c = f.a, f.b, f.c
            # the products are 32-bit (imul keeps eax only), the sum of squares 64-bit
            nx = s32(s32((b.y - a.y) * (c.z - a.z)) - s32((b.z - a.z) * (c.y - a.y)))
            ny = s32(s32((b.z - a.z) * (c.x - a.x)) - s32((b.x - a.x) * (c.z - a.z)))
            nz = s32(s32((b.x - a.x) * (c.y - a.y)) - s32((b.y - a.y) * (c.x - a.x)))
            sq = nx * nx + ny * ny + nz * nz
            hi = (sq >> 32) & 0xffffffff
            if hi in (0, 0xffffffff):
                ln = isqrt32(sq) or 1
                sh = 15
            else:
                ln = isqrt32(sq >> 8) or 1
                sh = 11
            n = [_clamp16(idiv(v << sh, ln)) for v in (nx, ny, nz)]
            f.nx, f.ny, f.nz = n
            ac = acc[id(a)]                      # only the face's first vertex gets the face normal
            ac[0] = s32(ac[0] + n[0])
            ac[1] = s32(ac[1] + n[1])
            ac[2] = s32(ac[2] + n[2])
        for v in o.verts:
            ax, ay, az = acc[id(v)]
            sq = ax * ax + ay * ay + az * az
            ln = isqrt32(sq >> 16) or 1
            v.nx, v.ny, v.nz = (_clamp16(idiv(c << 7, ln)) for c in (ax, ay, az))

    @staticmethod
    def _corners(o):
        """0x21d9ac: the 8 corners of the bounding box (min/max of the vertices' x, y, z words)."""
        xs = [v.x for v in o.verts]
        ys = [v.y for v in o.verts]
        zs = [v.z for v in o.verts]
        X = (min(xs + [0x7fff]), max(xs + [-0x7fff]))
        Y = (min(ys + [0x7fff]), max(ys + [-0x7fff]))
        Z = (min(zs + [0x7fff]), max(zs + [-0x7fff]))
        # corner k: x = X[k >= 4], y = Y[k in (2, 3, 6, 7)], z = Z[k odd]  (init values 0x7fff = min, 0x8001 = max)
        order = [(0, 0, 0), (0, 0, 1), (0, 1, 0), (0, 1, 1), (1, 0, 0), (1, 0, 1), (1, 1, 0), (1, 1, 1)]
        o.corners = [[X[i], Y[j], Z[k], 0, 0, 0, 0, 0] for i, j, k in order]


# ---------------------------------------------------------------- the animation (ANI) and its step 0x25f3ae
class Anim:
    """The ANI file: byte 0 (0), byte 1 = refreshes per key, dword +2 = number of delta keys; then key 0 (absolute)
    and the delta keys. Key: a word (unused), 6 dwords camera (key 0: x, y, z, ax, ay, az; delta keys: 16.16
    per-refresh increments of the same), then object records {word object, 6 dwords} ended by the word 0xffff.
    Object records: key 0: x, y, z (integers; the 3 angle dwords are ignored: angles start at 0); delta keys: 16.16
    per-refresh increments of x << 16, y << 16, z << 16 and of the angles << 16."""

    def __init__(self, data):
        self.data = data
        self.per = data[1]
        self.nkeys = struct.unpack_from('<I', data, 2)[0]
        self.keys = []
        p = 6
        while p < len(data):
            cam = struct.unpack_from('<6i', data, p + 2)
            p += 0x1a
            objs = []
            while True:
                i = struct.unpack_from('<H', data, p)[0]
                if i == 0xffff:
                    p += 2
                    break
                objs.append((i, struct.unpack_from('<6i', data, p + 2)))
                p += 0x1a
            self.keys.append((cam, objs))

    def reset(self, world):
        """The state as the done routine 0x20497a leaves it: key index 0, counter 0."""
        self.key = 0          # [0x21746c]
        self.cnt = 0          # [0x217470]
        self.ptr = 0          # [0x2174c2] (as a key number)
        self.cam = [0] * 6    # [0x217474..0x21747c] integer camera x, y, z; [0x21748c..94] angles
        self.camf = [0] * 6   # fractions [0x217480..88], [0x217498..a0]
        self.world = world

    def step(self):
        """0x25f3ae, once per refresh (and once in the init routine)."""
        w = self.world
        if self.key == 0:
            cam, objs = self.keys[0]
            self.cam = list(cam)
            self.camf = [0] * 6
            for i, d in objs:
                o = w.objects[i]
                o.pos = [d[0], d[1], d[2]]
                o.pos16 = [s32(d[0] << 16), s32(d[1] << 16), s32(d[2] << 16)]
                o.ang = [0, 0, 0]
            self.ptr = 1
            self.key = 1
            return
        cam, objs = self.keys[self.ptr]
        for k in range(6):
            d = cam[k]
            # 48-bit add: (int << 32 | frac32) += sign-extended (d << 16)
            frac = (self.camf[k] & 0xffffffff) + ((d << 16) & 0xffffffff)
            carry = frac >> 32
            self.camf[k] = frac & 0xffffffff
            self.cam[k] = s32(self.cam[k] + (d >> 16) + carry)
        for i, d in objs:
            o = w.objects[i]
            o.pos16 = [s32(o.pos16[k] + d[k]) for k in range(3)]
            o.ang = [s32(o.ang[k] + d[3 + k]) for k in range(3)]
        self.cnt = (self.cnt + 1) & 0xff
        if self.cnt == self.per:
            self.cnt = 0
            k = self.key + 1
            if k > self.nkeys:
                k = 0
            self.key = k
            self.ptr += 1


# ---------------------------------------------------------------- the renderer
class Renderer:
    W, H = 320, 200

    def __init__(self, world, texture, gouraud, part4=False, outcodes=None):
        self.world = world
        self.tex = texture           # the 64 KB buffer [0x21b140] (TEXTURE.RAW's 256 x 256 pixels)
        self.gouraud = gouraud       # 0x21643c: GFX\GOURAUD.PAL, 16 levels x 256
        self.part4 = part4           # [0x21d43c] = 0x21dfb8 and [0x2174c6] = 1 (PART4)
        self.outcodes = bytearray(outcodes or bytes(8))   # 0x21eea8: persists across objects and frames
        self.mask = 0x3f3f           # [0x21b148]
        self.win = (0, 319, 0, 199)  # [0x21ee98], [0x21ee9c], [0x21eea0], [0x21eea4]
        self.trace = None

    # -- camera 0x21db9c
    def camera(self, cam):
        a = s32(-cam[5]) & 0x1ffe          # [esi+8] = -angle z
        b = s32(cam[3] + 0x1000) & 0x1ffe  # [esi+4] = angle x + 0x1000
        c = s32(-cam[4]) & 0x1ffe          # [esi] = -angle y
        Sa, Ca, Sb, Cb, Sc, Cc = S(a), C(a), S(b), C(b), S(c), C(c)
        M = [0] * 9
        M[0] = s16(-s16(mul15(Ca, Sb) + mul15(mul15(Cb, Sc), Sa)))
        M[3] = s16(mul15(Sb, Sa) - mul15(mul15(Cb, Sc), Ca))
        M[6] = s16(-mul15(Cb, Cc))
        M[1] = s16(mul15(Ca, Cb) - mul15(mul15(Sb, Sc), Sa))
        M[4] = s16(-s16(mul15(Cb, Sa) + mul15(mul15(Sb, Sc), Ca)))
        M[7] = s16(-mul15(Sb, Cc))
        M[2] = mul15(Sa, Cc)
        M[5] = mul15(Ca, Cc)
        M[8] = s16(-Sc)
        h = [s32(-cam[k]) >> 1 for k in range(3)]       # [esi+0xc..0x14]: neg, sar 1
        T = [s32(M[3 * r] * h[0] + M[3 * r + 1] * h[1] + M[3 * r + 2] * h[2]) for r in range(3)]
        self.M, self.T, self.h = M, T, h

    # -- object rotation 0x21e354 (angles as table offsets)
    @staticmethod
    def rotation(a, b, c):
        Sa, Ca, Sb, Cb, Sc, Cc = S(a), C(a), S(b), C(b), S(c), C(c)
        R = [0] * 9
        R[8] = s16(mul15(Ca, Cb) - mul15(mul15(Sa, Sb), Sc))
        R[7] = s16(mul15(Sa, Cb) + mul15(mul15(Ca, Sc), Sb))
        R[6] = s16(-mul15(Sb, Cc))
        R[5] = s16(-mul15(Sa, Cc))
        R[4] = mul15(Cc, Ca)
        R[3] = Sc
        R[2] = s16(mul15(Ca, Sb) + mul15(mul15(Sa, Sc), Cb))
        R[1] = s16(mul15(Sa, Sb) - mul15(mul15(Ca, Sc), Cb))
        R[0] = mul15(Cc, Cb)
        return R

    def object_setup(self, o):
        """[0x21d43c]: 0x21ddf8 (0x21dfb8 in PART4). Returns True when the object is drawn."""
        M, T = self.M, self.T
        a, b, c = ((o.ang[k] >> 16) & 0x1ffe for k in range(3))
        R = self.rotation(a, b, c)
        Cm = [0] * 9
        for r in range(3):
            for k in range(3):
                Cm[3 * r + k] = s32((R[k] * M[3 * r] + R[3 + k] * M[3 * r + 1] + R[6 + k] * M[3 * r + 2]) >> 15)
        o.Cm = Cm
        o.R = self.rotation((-(o.ang[0] >> 16)) & 0x1ffe, (-(o.ang[1] >> 16)) & 0x1ffe, (-(o.ang[2] >> 16)) & 0x1ffe)
        # 0x21e61c
        for r in range(3):
            v = M[3 * r] * o.pos[0] + M[3 * r + 1] * o.pos[1] + M[3 * r + 2] * o.pos[2] + T[r]
            if (v >> 32) not in (0, -1):
                return False
            o.P[r] = s32(v)
        if o.P[2] >= 0x1b580000:
            return False
        k = 0
        for cn in o.corners:
            xyz = []
            for r in range(3):
                v = M[3 * r] * cn[0] + M[3 * r + 1] * cn[1] + M[3 * r + 2] * cn[2] + s32(T[r] + o.P[r])
                v >>= 15
                if (v >> 32) not in (0, -1):
                    return False
                xyz.append(s32(v))
            cn[3:6] = xyz
            x, y, z = xyz
            if z > 0x80:
                sx = idiv(x << 8, z) + 0xa0
                sy = 0x64 - idiv(y << 8, z)
                cn[6], cn[7] = s16(sx), s16(sy)
                sx32, sy32 = s32(sx), s32(sy)
                code = (1 if sx32 < 0 else 0) | (2 if s32(0x140 - sx32) < 0 else 0) | \
                       (4 if sy32 < 0 else 0) | (8 if s32(0xc8 - sy32) < 0 else 0)
                if code == 0:
                    return True
                self.outcodes[k] = code
                k += 1
        if self.part4:
            return False
        # with k < 8 corners stored, bytes k..7 are left from earlier objects or frames: they decide when the
        # stored codes are all equal
        self.stale_decides = k < 8 and all(x == self.outcodes[0] for x in self.outcodes[:k])
        return not all(x == self.outcodes[0] for x in self.outcodes)

    # -- vertices 0x21e54c
    def transform(self, o):
        Cm, P, T = o.Cm, o.P, self.T
        t = [s32(P[r] + T[r]) for r in range(3)]
        for v in o.verts:
            v.shade = 0xffff
            X = s32(s32(v.x * Cm[0]) + s32(v.y * Cm[1]) + t[0] + s32(v.z * Cm[2])) >> 15
            Y = s32(s32(v.x * Cm[3]) + s32(v.y * Cm[4]) + t[1] + s32(v.z * Cm[5])) >> 15
            Z = s32(s32(v.x * Cm[6]) + s32(v.y * Cm[7]) + t[2] + s32(v.z * Cm[8])) >> 15
            v.xc, v.yc, v.zc, v.zc32 = s16(X), s16(Y), s16(Z), Z
            if Z > 0x80:
                v.sx = s16(idiv(v.xc << 8, Z) + 0xa0)
                v.sy = s16(0x64 - idiv(v.yc << 8, Z))
                v.clip = 0
            else:
                v.clip = 1

    def light_point(self, o):
        """0x21e9bc: the lighting point L (the camera, in object space through the angles' negatives)."""
        e = [s32(-2 * self.h[k] - o.pos[k]) for k in range(3)]
        R = o.R
        return [s16(s32(R[3 * r] * e[0] + R[3 * r + 1] * e[1] + R[3 * r + 2] * e[2]) >> 15) for r in range(3)]

    @staticmethod
    def _light(L, p, n, dshift):
        d = [s16(L[k] - p[k]) for k in range(3)]
        dd = s32(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
        dot = s16(((d[0] * n[0]) >> 16) + ((d[1] * n[1]) >> 16) + ((d[2] * n[2]) >> 16))
        if dot <= 0:
            return None
        q = idiv(dot * dot << 10, dd) & 0xffff
        al = s8(isqrt_table(q) - isqrt_table((dd & 0xffffffff) >> dshift))
        return al

    def flat_colour(self, f, L):
        """0x21e72c: the point is the 6 bytes at 0x2401f8, which nothing writes: (0, 0, 0)."""
        al = self._light(L, (0, 0, 0), (f.nx, f.ny, f.nz), 18)
        if al is None or al < 0 or al > 15:
            return (f.colour + 15) & 0xff
        return (f.colour + 15 - al) & 0xff

    def vertex_shade(self, v, L):
        """0x21e7c8: shade level 0 (bright) .. 15 (dark)."""
        al = self._light(L, (v.x, v.y, v.z), (v.nx, v.ny, v.nz), 20)
        if al is None or al < 0:
            return 15
        if al > 15:
            return 0
        return 15 - al

    # -- near clipping 0x21e864
    def near_clip(self, f, pool):
        out = []
        prev = f.c
        for cur in (f.a, f.b, f.c):
            if (prev.clip ^ cur.clip) & 1:
                n = Vertex(cur.x, cur.y, cur.z, 0, 0)    # u, v (+6, +8) and the shade (+0x1a) are never written: 0
                n.shade = 0
                n.zc = cur.zc
                t = idiv(s32((-(cur.zc - 0x80)) << 16), s32(prev.zc - cur.zc))
                n.xc = cur.xc
                x = s32(((prev.xc - cur.xc) * t) >> 16) + cur.xc
                n.sx = s16(2 * x + 0xa0)
                n.yc = cur.yc
                y = s32(((prev.yc - cur.yc) * t) >> 16) + cur.yc
                n.sy = s16(-2 * y + 0x64)
                n.clip = 0
                pool.append(n)
                out.append(n)
            prev = cur
            if not (cur.clip & 1):
                out.append(cur)
        faces = []
        for i in range(1, len(out) - 1):
            g = Face(f.type, f.colour, f.tex, out[0], out[i], out[i + 1])
            g.nx, g.ny, g.nz = f.nx, f.ny, f.nz
            faces.append(g)
        return faces

    @staticmethod
    def front(f):
        a, b, c = f.a, f.b, f.c
        p1 = s16(b.sx - a.sx) * s16(c.sy - b.sy)
        p2 = s16(b.sy - a.sy) * s16(c.sx - b.sx)
        return s32(p1 - p2) < 0

    # -- the whole render 0x25f5b5
    def render(self, anim, buf=None):
        """Renders the state in anim (camera) and the world's objects into buf (320 x 200 indices, not cleared:
        the frame routines clear it after use). Returns buf."""
        if buf is None:
            buf = bytearray(64000)
        self.buf = buf
        self.camera(anim.cam)
        vis = []
        for i, o in enumerate(self.world.objects):
            o.pos = [o.pos16[k] >> 16 for k in range(3)]
            if self.object_setup(o):
                vis.append((s32((o.P[2] & 0xffff0000) + len(vis)), o))
        vis.sort(key=lambda e: -e[0])
        self.drawn_objects = [o for k, o in vis]
        self.drawn_faces = 0
        self.stats = {'near-clipped faces drawn': 0, 'screen-clipped polygons': 0, 'flat faces': 0,
                      'gouraud faces': 0, 'texture faces': 0, 'fan triangles': 0}
        for key, o in vis:
            self.draw_object(o)
        if self.part4:
            self.boxes()
        return buf

    def draw_object(self, o):
        L = self.light_point(o)
        self.transform(o)
        lst = []
        pool = []
        clipped = []
        n = 0
        for f in o.faces:
            if f.a.clip or f.b.clip or f.c.clip:
                clipped += self.near_clip(f, pool)
                continue
            if self.front(f):
                lst.append((s32(((f.a.zc & 0xffff) << 16) + n), f))
                n += 1
        for f in clipped:
            f.nearclip = True
            if self.front(f):
                lst.append((s32(((f.a.zc & 0xffff) << 16) + n), f))
                n += 1
        lst.sort(key=lambda e: -e[0])
        for key, f in lst:
            self.draw_face(f, L)
            self.drawn_faces += 1

    def draw_face(self, f, L):
        t = f.type
        if getattr(f, 'nearclip', False):
            self.stats['near-clipped faces drawn'] += 1
        self.stats[{0: 'flat faces', 1: 'flat faces', 6: 'texture faces', 7: 'texture faces'}.get(t, 'gouraud faces')] += 1
        if t in (0, 1):
            col = self.flat_colour(f, L)
            pts = [[v.sx, v.sy, 0, 0, 0] for v in (f.a, f.b, f.c)]
            self.poly_flat(pts, col)
            return
        if t in (2, 3, 4, 5, 8, 9, 10, 11):
            for v in (f.a, f.b, f.c):
                if v.shade == 0xffff:
                    v.shade = self.vertex_shade(v, L)
        base = ((f.tex & 0xf0) << 8) + ((f.tex << 4) & 0xff)
        if t in (6, 7):
            pts = [[v.sx, v.sy, v.u & 0xffff, v.v & 0xffff, 0] for v in (f.a, f.b, f.c)]
            self.poly_tex(pts, base, False)
        elif t == 8:
            pts = [[v.sx, v.sy, v.u & 0xffff, v.v & 0xffff, v.shade & 0xffff] for v in (f.a, f.b, f.c)]
            self.poly_tex(pts, base, True)
        else:
            raise NotImplementedError('face type %d' % t)

    # ---------------------------------------------------------------- screen clipping
    def _clip(self, pts, axis, bound, keep_le, flags):
        """0x21b14c (x >= bound), 0x21b42c (x <= bound), 0x21b70c (y >= bound), 0x21b9ec (y <= bound).
        pts: [x, y, u, v, s]. The intersection is interpolated from the previous vertex toward the current one."""
        other = 1 - axis

        def outside(p):
            return p[axis] > bound if keep_le else p[axis] < bound

        out = []
        prev = pts[-1]
        pout = outside(prev)
        for cur in pts:
            cout = outside(cur)
            if cout != pout:
                num = bound - prev[axis]
                den = cur[axis] - prev[axis]
                q = [0, 0, 0, 0, 0]
                q[axis] = bound
                val = s32(idiv(s32((cur[other] - prev[other]) * num) << 16, den) + (prev[other] << 16))
                q[other] = s16(val >> 16)
                if flags & 2:
                    val = s32(idiv(s32((cur[4] - prev[4]) * num) << 16, den) + (prev[4] << 16))
                    q[4] = (val >> 16) & 0xffff
                else:
                    q[4] = 0
                if flags & 4:
                    for k in (2, 3):
                        sl = idiv(s32((cur[k] - prev[k]) << 8), s16(den))
                        q[k] = ((s32(sl * s16(num)) >> 8) + prev[k]) & 0xffff
                out.append(q)
            if not cout:
                out.append(list(cur))
            prev, pout = cur, cout
        return out

    def _clipped(self, pts, flags):
        """The bounding-box tests and clips of 0x21bd4c / 0x21c6b4 / 0x21cc28 (restart after each clip).
        Returns None when nothing is drawn."""
        x0, x1, y0, y1 = self.win
        clipped = False
        while True:
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            if not pts:
                return None
            mnx, mxx, mny, mxy = min(xs), max(xs), min(ys), max(ys)
            if mny >= mxy or mny > y1 or mxy < y0 or mnx > x1 or mxx < x0:
                return None
            if mnx < x0:
                pts = self._clip(pts, 0, x0, False, flags)
            elif mxx > x1 + 1:
                pts = self._clip(pts, 0, x1 + 1, True, flags)
            elif mny < y0:
                pts = self._clip(pts, 1, y0, False, flags)
            elif mxy > y1:
                pts = self._clip(pts, 1, y1, True, flags)
            else:
                if len(pts) != 3 or clipped:
                    self.stats['screen-clipped polygons'] += 1
                return pts
            clipped = True

    # ---------------------------------------------------------------- the edge walker shared by all rasterizers
    def _walk(self, pts, span, flags):
        """0x21bea4 / 0x21c8b0 / 0x21ce1c: pts in order (front faces run counter-clockwise on screen: the next
        vertex is the left edge). Calls span(y, xl, xr, ul, vl, sl, g) for every drawn row, g = the gradients."""
        n = len(pts)
        top = 0
        for i in range(1, n):
            if pts[i][1] < pts[top][1]:
                top = i
        y = pts[top][1]
        L = top          # [0x21a4a8]
        R = top          # [0x21a4b0]
        Lnext = (top + 1) % n
        Rnext = (top - 1) % n
        need = 3
        xl = xr = 0
        sxl = sxr = 0
        ul = vl = sl = 0
        sul = svl = ssl = sur = svr = ssr = 0
        tex = flags & 4
        sh = flags & 2
        while True:
            if need & 1:
                while True:
                    a = pts[L]
                    b = pts[Lnext]
                    L = Lnext
                    Lnext = (Lnext + 1) % n
                    dy = s16(b[1] - a[1])
                    if dy == 0:
                        continue
                    if dy < 0:
                        return
                    break
                xl = s32((a[0] << 16) + 0x8000)
                sxl = idiv(s32(s16(b[0] - a[0]) << 16), dy) | 1
                if tex:
                    ul = a[2] << 8
                    sul = idiv(s32((b[2] << 8) - ul), dy)
                    vl = a[3] << 8
                    svl = idiv(s32((b[3] << 8) - vl), dy)
                if sh:
                    sl = s32(a[4] << 16)
                    ssl = idiv(s32((b[4] << 16) - (a[4] << 16)), dy)
            if need & 2:
                while True:
                    a = pts[R]
                    b = pts[Rnext]
                    R = Rnext
                    Rnext = (Rnext - 1) % n
                    dy = s16(b[1] - a[1])
                    if dy == 0:
                        continue
                    if dy < 0:
                        return
                    break
                xr = s32((a[0] << 16) + 0x8000)
                sxr = idiv(s32(s16(b[0] - a[0]) << 16), dy) | 1
                if tex:
                    sur = idiv(s32((b[2] - a[2]) << 8), dy)
                    svr = idiv(s32((b[3] - a[3]) << 8), dy)
                if sh:
                    ssr = idiv(s32((b[4] - a[4]) << 16), dy)
            yl, yr = pts[L][1], pts[R][1]
            if yl < yr:
                cnt, need = s16(yl - y), 1
            elif yl > yr:
                cnt, need = s16(yr - y), 2
            else:
                cnt, need = s16(yl - y), 3
            g = None
            if tex:
                den = s32(sxr - sxl) or 0x7fff
                gu = idiv(s32(sur - sul) << 16, den)
                gv = idiv(s32(svr - svl) << 16, den)
                gs = idiv(s32(ssr - ssl) << 16, den) if sh else 0
                for q in (gu, gv, gs):
                    if not -0x80000000 <= q < 0x80000000:
                        raise OverflowError('divide overflow (#DE) in the gradient')
                g = (gu, gv, gs)
            for k in range(cnt + 1):
                span(y + k, xl, xr, ul, vl, sl, g)
                if k < cnt:
                    xl = s32(xl + sxl)
                    xr = s32(xr + sxr)
                    ul = s32(ul + sul)
                    vl = s32(vl + svl)
                    sl = s32(sl + ssl)
            y += cnt

    def poly_flat(self, pts, colour):
        """0x21bce0 → 0x21bd4c: a flat n-gon (no fan split), span 0x21c05c: pixels [xl >> 16, xr >> 16)."""
        pts = self._clipped(pts, 1)
        if pts is None:
            return
        buf = self.buf

        def span(y, xl, xr, ul, vl, sl, g):
            a, b = (xl & 0xffffffff) >> 16, (xr & 0xffffffff) >> 16
            if b > a:
                o = y * 320
                buf[o + a:o + b] = bytes([colour]) * (b - a)
        self._walk(pts, span, 1)

    def poly_tex(self, pts, base, gouraud):
        """0x21c6b4 (texture, span 0x21cb3c) / 0x21cc28 (texture + shade, span 0x21d0fc): triangles; clipped
        polygons are drawn as a fan (0, 1, 2), (0, 2, 3), ..."""
        flags = 7 if gouraud else 5
        pts = self._clipped(pts, flags)
        if pts is None:
            return
        buf, tex, gour, mask = self.buf, self.tex, self.gouraud, self.mask
        mu, mv = mask & 0xff, (mask >> 8) & 0xff

        def span(y, xl, xr, ul, vl, sl, g):
            a, b = (xl & 0xffffffff) >> 16, (xr & 0xffffffff) >> 16
            if b <= a:
                return
            gu, gv, gs = g
            c = (-xl) & 0xffff
            u = ul + ((gu * c) >> 16)
            v = vl + ((gv * c) >> 16)
            o = y * 320 + a
            if not gouraud:
                for i in range(b - a):
                    t = tex[base + (((v >> 16) & mv) << 8) + ((u >> 16) & mu)]
                    buf[o + i] = t
                    u += gu
                    v += gv
            else:
                # edx = V frac << 16 | shade integer (16 bits): the shade's carries can reach V's fraction
                vf = v & 0xffff
                vi = (v >> 16) & 0xff
                sfrac = sl & 0xffff
                sint = (sl >> 16) & 0xffff
                gsf, gsi = gs & 0xffff, (gs >> 16) & 0xff
                gvf, gvi = gv & 0xffff, (gv >> 16) & 0xff
                for i in range(b - a):
                    t = tex[base + ((vi & mv) << 8) + ((u >> 16) & mu)]
                    lev = sint & 0xff
                    buf[o + i] = gour[lev * 256 + t]
                    u += gu
                    sfrac += gsf
                    cs = sfrac >> 16
                    sfrac &= 0xffff
                    edx = ((vf << 16) | sint) + ((gvf << 16) | gsi) + cs
                    cv = edx >> 32
                    edx &= 0xffffffff
                    vf, sint = edx >> 16, edx & 0xffff
                    vi = (vi + gvi + cv) & 0xff
        while True:
            tri = pts[:3]
            ys = [p[1] for p in tri]
            if not (ys[1] == ys[0] and ys[2] == ys[0]):
                self._walk(tri, span, flags)
            if len(pts) == 3:
                return
            self.stats['fan triangles'] += 1
            pts = [pts[0]] + pts[2:]

    # ---------------------------------------------------------------- PART4's boxes 0x25f26e
    def boxes(self):
        for o in self.world.objects:
            t = [s32(self.T[r] + o.P[r]) for r in range(3)]
            last_front = False
            for cn in o.corners:
                xyz = []
                for r in range(3):
                    v = o.Cm[3 * r] * cn[0] + o.Cm[3 * r + 1] * cn[1] + o.Cm[3 * r + 2] * cn[2] + t[r]
                    xyz.append(s32(v >> 15))
                cn[3:6] = xyz
                last_front = xyz[2] > 0x80
                if last_front:
                    cn[6] = s16(idiv(xyz[0] << 8, xyz[2]) + 0xa0)
                    cn[7] = s16(0x64 - idiv(xyz[1] << 8, xyz[2]))
            if last_front:
                p = [(c[6], c[7]) for c in o.corners]
                for i, j in ((0, 1), (2, 3), (1, 3), (0, 2), (4, 5), (6, 7), (5, 7), (4, 6), (0, 4), (1, 5), (2, 6),
                             (3, 7)):
                    self.line(p[i][0], p[i][1], p[j][0], p[j][1])

    def line(self, x0, y0, x1, y1, colour=0x80):
        """0x21d22c: clipped to 0..319 x 0..199 (restarting after each end is moved), then a Bresenham-like walk
        of max(|dx|, |dy|) pixels (the far end is not plotted, a zero-length line plots one pixel), colour 0x80."""
        while True:
            dx = (x1 - x0) or 1
            dy = (y1 - y0) or 1
            if x0 < 0:
                if x1 <= 0:
                    return
                y0 -= s32(idiv(s32(dy << 16), dx) * x0) >> 16
                x0 = 0
            elif x0 >= 320:
                if x1 >= 320:
                    return
                y0 -= s32(idiv(s32(dy << 16), dx) * (x0 - 319)) >> 16
                x0 = 319
            elif x1 < 0:
                y1 -= s32(idiv(s32(dy << 16), dx) * x1) >> 16
                x1 = 0
            elif x1 >= 320:
                y1 -= s32(idiv(s32(dy << 16), dx) * (x1 - 319)) >> 16
                x1 = 319
            elif y0 < 0:
                if y1 <= 0:
                    return
                x0 -= s32((idiv(s32(dx << 16), dy) * y0) >> 16)
                if x0 < 0 or x0 >= 320:
                    return
                y0 = 0
            elif y0 >= 200:
                if y1 >= 200:
                    return
                x0 -= s32((idiv(s32(dx << 16), dy) * (y0 - 199)) >> 16)
                if x0 < 0 or x0 >= 320:
                    return
                y0 = 199
            elif y1 < 0:
                x1 -= s32((idiv(s32(dx << 16), dy) * y1) >> 16)
                if x1 < 0 or x1 >= 320:
                    return
                y1 = 0
            elif y1 >= 200:
                x1 -= s32((idiv(s32(dx << 16), dy) * (y1 - 199)) >> 16)
                if x1 < 0 or x1 >= 320:
                    return
                y1 = 199
            else:
                break
        ax, sx = (x1 - x0, 1) if x1 >= x0 else (x0 - x1, -1)
        ay, sy = (y1 - y0, 320) if y1 >= y0 else (y0 - y1, -320)
        p = y0 * 320 + x0
        buf = self.buf
        if ax > ay:
            e, n = ax >> 1, ax
            while True:
                buf[p] = colour
                p += sx
                e += ay
                if e >= ax:
                    e -= ax
                    p += sy
                n -= 1
                if n <= 0:
                    break
        else:
            e, n = ay >> 1, ay
            while True:
                buf[p] = colour
                p += sy
                e += ax
                if e >= ay:
                    e -= ay
                    p += sx
                n -= 1
                if n <= 0:
                    break


# ---------------------------------------------------------------- full-screen passes
def aa_pass(src, dst, T):
    """0x2045fa: dst(x, y) for 1 <= y <= 198, 4 <= x <= 315 (the rest of dst is not written):
    even x: T[T[up][down]][T[c][left]]; odd x: T[T[down][up]][T[left][c]]   (T[a][b] = table byte a * 256 + b)."""
    for y in range(1, 199):
        o = y * 320
        for x in range(4, 316):
            c, l, u, d = src[o + x], src[o + x - 1], src[o + x - 320], src[o + x + 320]
            if x & 1:
                dst[o + x] = T[T[d * 256 + u] * 256 + T[l * 256 + c]]
            else:
                dst[o + x] = T[T[u * 256 + d] * 256 + T[c * 256 + l]]


def fin_pass(new, screen, T):
    """0x2046c7: screen[i] = T[new[i]][screen[i]] for every pixel."""
    for i in range(64000):
        screen[i] = T[new[i] * 256 + screen[i]]
