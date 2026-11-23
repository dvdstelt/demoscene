"""Python model of SurRender P5 1.02's render path as INSIDE uses it (notes-engine.md), written to reproduce the
original's integer results: the object pipeline 0x423a84 (0x40c158, 0x41a458, 0x41afa0, 0x41a9d4, 0x41aba4 /
0x41b508), the radix sort 0x40c384, the near-plane clipper 0x41c0e0, the triangle setup 0x41be91 and the affine
texture + Gouraud rasterizer 0x414e5c (shade table lookup).

The FPU runs with precision control = 53 bits (control word 0x127f, set by Watcom's start-up through 0x41d4b8), so
every x87 operation rounds to a double: the model uses doubles (numpy float64) in the original's operation order, and
float -> int conversions truncate toward zero (0x40ba78).
"""
import math

import numpy as np

L = np.float64


def D(x):
    """a value stored as a qword (with precision control 53 bits this changes nothing)"""
    return np.float64(x)


def trunc(x):
    """float -> int toward zero (0x40ba78 then fistp)"""
    if isinstance(x, np.floating):
        return int(np.trunc(x))
    return int(math.trunc(x))


def tdiv(a, b):
    """C integer division (idiv): truncation toward zero"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x & 0x80000000 else x


# ----------------------------------------------------------------------------------------------- matrices (3x3)

def mat_mul(A, B):
    """0x414014 / 0x414070: (A B)[i][j] = (A[i][1] B[1][j] + A[i][0] B[0][j]) + A[i][2] B[2][j], stored as doubles"""
    return [[D((A[i][1] * B[1][j] + A[i][0] * B[0][j]) + A[i][2] * B[2][j]) for j in range(3)] for i in range(3)]


def mat_vec(M, v):
    """0x4144ec: v' = M v (column vector)"""
    return [D((v[0] * M[i][0] + v[1] * M[i][1]) + v[2] * M[i][2]) for i in range(3)]


def transpose(M):
    return [[M[j][i] for j in range(3)] for i in range(3)]


def euler(a, b, c):
    """0x4145f4: I . Rx(a) . Ry(b) . Rz(c) (right-handed, column vectors)"""
    M = [[L(1), L(0), L(0)], [L(0), L(1), L(0)], [L(0), L(0), L(1)]]
    M = rot_x(M, a)
    M = rot_y(M, b)
    return rot_z(M, c)


def rot_x(M, a):       # 0x414270: M . Rx(a)
    c, s = L(math.cos(a)), L(math.sin(a))
    return [[r[0], D(r[1] * c + r[2] * s), D(r[2] * c - r[1] * s)] for r in M]


def rot_y(M, a):       # 0x414344: M . Ry(a)
    c, s = L(math.cos(a)), L(math.sin(a))
    return [[D(r[0] * c - r[2] * s), r[1], D(r[2] * c + r[0] * s)] for r in M]


def rot_z(M, a):       # 0x414418: M . Rz(a)
    c, s = L(math.cos(a)), L(math.sin(a))
    return [[D(r[0] * c + r[1] * s), D(r[1] * c - r[0] * s), r[2]] for r in M]


# --------------------------------------------------------------------------------------------------- the scene

class Scene:
    """the scene's view parameters as 0x40c4e4 latches them"""

    def __init__(self, width=320, height=200, x0=0, x1=320, y0=0, y1=200, fov=0x100, near=0x2000, ambient=0,
                 flags=0, cx=None, cy=None, far=0x400000):
        self.width, self.height = width, height
        self.x0, self.y0 = max(x0, 0), max(y0, 0)
        self.x1, self.y1 = min(x1, width), min(y1, height)
        if not flags & 2:
            cy = y0 + tdiv(y1 - y0, 2)
            cx = x0 + tdiv(x1 - x0, 2)
        self.cx, self.cy = cx, cy
        w, h = (x1 - x0, y1 - y0) if flags & 4 else (width, height)
        f = L(fov)
        a = L(w) * L(32768)
        b = L(h) * L(32768)
        self.xs = trunc(a * f * D(L(1) / L(81920)))       # [0x4b9014]: 128 fov w / 320
        self.ys = trunc(b * f * D(L(1) / L(61440)))       # [0x4b8fec]: 128 fov h / 240
        self.near = near
        self.far = far
        self.ambient = ambient


def object_to_camera(cam_mat, cam_pos, obj_mat, obj_pos):
    """0x40c158: T = trunc(128 Cam^T (obj - cam)) (ints), M = Cam^T Obj (doubles)"""
    inv = transpose(cam_mat)
    rel = [L(obj_pos[k] - cam_pos[k]) for k in range(3)]
    v = mat_vec(inv, rel)
    T = [trunc(L(128) * v[k]) for k in range(3)]
    return mat_mul(inv, obj_mat), T


class Vertex:
    __slots__ = ('x', 'y', 'z', 'u', 'v', 'eu', 'ev', 'n', 'sx', 'sy', 'zc', 'px', 'py', 'shade', 'shade2', 'flags',
                 'idx')


def make_vertices(verts, vnormals):
    out = []
    for i, ((x, y, z, u, v), n) in enumerate(zip(verts, vnormals)):
        p = Vertex()
        p.x, p.y, p.z, p.u, p.v, p.n, p.idx = x, y, z, u, v, n, i
        p.eu = p.ev = 0
        p.shade = p.shade2 = 0
        p.flags = 0
        out.append(p)
    return out


def backface(vs, faces, fnormals, obj_mat, obj_pos, cam_pos, double_sided=()):
    """0x41a458: a face is drawn when (eye - p0) . n >= 0, eye = the camera in object space"""
    rel = [L(cam_pos[k] - obj_pos[k]) for k in range(3)]
    eye = mat_vec(transpose(obj_mat), rel)
    vis = []
    for i, ((a, b, c), n) in enumerate(zip(faces, fnormals)):
        p = vs[a]
        if i in double_sided:
            vis.append(True)
            continue
        d = ((eye[1] - L(p.y)) * L(n[1]) + (eye[0] - L(p.x)) * L(n[0])) + (eye[2] - L(p.z)) * L(n[2])
        vis.append(bool(d >= 0))
    return vis


def project(vs, used, M, T, sc):
    """0x41afa0 for the vertices in `used`: camera-space Z (x128), the projection numerators X'/2048, Y'/2048,
    the screen position in 1/16 pixel (truncated) and the clip flags; the same operation order as the original."""
    c = D(L(1) / L(256))
    xsc = L(sc.xs) * c
    ysc = L(sc.ys) * c
    P = xsc * L(2048)
    Q = ysc * L(2048)
    m = [[None] * 3 for _ in range(3)]
    m[0] = [D(M[0][k] * P) for k in range(3)]
    m[1] = [D(M[1][k] * D(Q)) for k in range(3)]
    m[2] = [D(M[2][k] * L(128)) for k in range(3)]
    xscd, yscd = D(xsc), D(ysc)
    tx = (L(16) * L(T[0])) * xscd
    ty = (L(16) * L(T[1])) * yscd
    cz = D(-m[2][0] * m[2][1] + L(T[2]))
    cxx = D(tx + (-m[0][0] * m[0][1]))
    cyy = D(ty + (-m[1][0] * m[1][1]))
    cx16, cy16 = L(sc.cx * 16), L(sc.cy * 16)
    for p in vs:
        if p.idx not in used:
            continue
        x, y, z = L(p.x), L(p.y), L(p.z)
        nxy = -x * y
        Z = (((x + m[2][1]) * (y + m[2][0]) + cz) + nxy) + z * m[2][2]
        Zd = D(Z)
        p.zc = trunc(Z)
        X = (((x + m[0][1]) * (y + m[0][0]) + cxx) + nxy) + z * m[0][2]
        Xd = D(X)
        p.px = tdiv(trunc(X), 2048)
        Y = (((y + m[1][0]) * (x + m[1][1]) + cyy) + nxy) + z * m[1][2]
        Yd = D(Y)
        p.py = tdiv(trunc(Y), 2048)
        fl = 0
        if sc.near > p.zc:
            fl = 0x100000
            p.sx = p.sy = None
        else:
            q = D(L(2) / Zd)
            sx = D(Xd * q + cx16)
            sy = Yd * q + cy16
            p.sx, p.sy = trunc(sx), trunc(sy)
            if p.sx < sc.x0 * 16: fl |= 0x10000
            if p.sx >= sc.x1 * 16: fl |= 0x20000
            if p.sy < sc.y0 * 16: fl |= 0x40000
            if p.sy >= sc.y1 * 16: fl |= 0x80000
        p.flags = fl


def light_vertices(vs, used, obj_mat, obj_pos, obj_lpos, obj_radius, lights, ambient):
    """0x41aba4 / 0x41b508: shade = ambient + sum over lights of max(0, (I - d I / R) (n . (L - p)) / (512 d)),
    truncated after each light, clamped to 992.  lights: [(world pos, local pos, R = [0xc8], I = [0xcc])]"""
    for p in vs:
        if p.idx in used:
            p.shade = ambient
    for (lw, ll, R, I) in lights:
        dd = math.sqrt(sum(float(L(obj_lpos[k] - ll[k])) ** 2 for k in range(3)))
        if trunc(L(dd)) > obj_radius + R:         # 0x41063c distance of the local positions, truncated
            continue
        rel = [L(lw[k] - obj_pos[k]) for k in range(3)]
        lo = mat_vec(transpose(obj_mat), rel)
        Id, Rd = L(I), L(R)
        IR = D(Id / Rd)
        for p in vs:
            if p.idx not in used:
                continue
            d = [D(lo[0] - L(p.x)), D(lo[1] - L(p.y)), D(lo[2] - L(p.z))]
            dist = np.sqrt((d[1] * d[1] + d[0] * d[0]) + d[2] * d[2])
            if dist > Rd:
                continue
            distd = D(dist)
            dot = D((L(p.n[1]) * d[1] + L(p.n[0]) * d[0]) + L(p.n[2]) * d[2])
            if L(0) > dot:
                continue
            q = D(dot / (distd * L(512)))
            at = Id if L(1) > distd else Id - distd * IR
            cval = D(at * q)
            if cval > 0:
                p.shade = s16(trunc(L(p.shade) + cval))
            if p.shade > 0x3e0:
                p.shade = 0x3e0


def s16(x):
    x &= 0xffff
    return x - 0x10000 if x & 0x8000 else x


class Face:
    __slots__ = ('v', 'key', 'idx', 'fn')


def face_list(vs, faces, vis, bias=0):
    """0x41a9d4: faces in the order the list is built (each new face is put in front), with the sort key
    trunc((z0 + z1 + z2) / 3 - bias) / 16 (>= 0); faces whose three vertices are outside one edge or behind the
    near plane are dropped."""
    lst = []
    third = D(L(1) / L(3))
    for i, f in enumerate(faces):
        if not vis[i]:
            continue
        a, b, c = (vs[k] for k in f)
        o = (a.flags | b.flags | c.flags) & 0xff0000
        n = (a.flags & b.flags & c.flags) & 0xff0000
        if o & n:
            continue
        if o and n:
            continue
        s = a.zc + b.zc + c.zc
        avg = trunc(L(s) * third)
        k = tdiv(avg - bias, 16)
        if k < 0:
            k = 0
        F = Face()
        F.v, F.key, F.idx = (a, b, c), k, i
        lst.insert(0, F)
    return lst


def sort_faces(lst):
    """0x40c384: three LSD passes over the key's bytes 0 (bucket 255 - byte), 1, 2 with linked-list buckets (each
    item is put in front of its bucket's list), then the buckets from 255 down to 0 are joined. Result: key
    descending (far first); equal keys in the order of the face list's reverse, i.e. ascending face index."""
    b1 = [[] for _ in range(256)]
    for it in lst:
        b1[(it.key & 0xff) ^ 0xff].insert(0, it)        # the first pass uses 255 - byte 0
    b2 = [[] for _ in range(256)]
    for b in b1:
        for it in b:
            b2[(it.key >> 8) & 0xff].insert(0, it)
    b3 = [[] for _ in range(256)]
    for b in b2:
        for it in b:
            b3[(it.key >> 16) & 0xff].insert(0, it)
    out = []
    for b in reversed(b3):
        out.extend(b)
    return out


# ------------------------------------------------------------------------------------------------- rasterizer

class RV:
    """a vertex as the rasterizer sees it: sx, sy (1/16 px), zc, px, py, u, v, eu, ev, shade, shade2"""
    __slots__ = ('sx', 'sy', 'zc', 'px', 'py', 'u', 'v', 'eu', 'ev', 'shade', 'shade2')


def rv(p):
    r = RV()
    for k in RV.__slots__:
        setattr(r, k, getattr(p, k))
    return r


def _lerp_fix(t, e, s):
    return ((t * e) >> 12) + (((4096 - t) * s) >> 12)


def _clip_point(s, e, sc):
    """0x41c0e0: the point of edge s (in front) -> e (behind) at z = near, 4.12 weights"""
    t = 0
    if s.zc - e.zc != 0:
        k = tdiv(1 << 30, s.zc - e.zc)
        t = ((s.zc - sc.near) * k) >> 18
    K = tdiv(1 << 30, sc.near >> 8)
    r = RV()
    r.sx = ((_lerp_fix(t, e.px, s.px)) * K >> 26) + sc.cx * 16
    r.sy = ((_lerp_fix(t, e.py, s.py)) * K >> 26) + sc.cy * 16
    for k in ('u', 'v', 'eu', 'ev', 'shade', 'shade2'):
        setattr(r, k, s16((t * getattr(e, k) + (4096 - t) * getattr(s, k)) >> 12))
    r.zc = s32((t * e.zc + (4096 - t) * s.zc) >> 12)
    r.px = r.py = 0
    return r


def clip_and_draw(tri, sc, draw):
    """0x41be3c / 0x41c0e0: count the vertices behind the near plane; 0: draw; 3: nothing; 2: one triangle;
    1: two triangles."""
    a, b, c = tri
    n = sum(1 for p in tri if p.zc < sc.near)
    if n == 0:
        draw(a, b, c)
        return
    if n == 3:
        return
    esi, edi, ebx = a, b, c
    if n == 2:                       # sort by z descending: esi in front
        if esi.zc < edi.zc: esi, edi = edi, esi
        if edi.zc < ebx.zc: edi, ebx = ebx, edi
        if esi.zc < edi.zc: esi, edi = edi, esi
        v1 = _clip_point(esi, edi, sc)
        v0 = _clip_point(esi, ebx, sc)
        draw(esi, v1, v0)
    else:                            # ebx = the vertex behind
        if esi.zc < sc.near: esi, ebx = ebx, esi
        if edi.zc < sc.near: edi, ebx = ebx, edi
        v0 = _clip_point(esi, ebx, sc)
        v3 = _clip_point(edi, ebx, sc)
        draw(esi, edi, v0)
        draw(edi, v0, v3)


def setup(v0, v1, v2, sc):
    """0x41be91: sort by y, edges, the width at the middle vertex, the prestep; None when rejected"""
    a, b, c = v0, v1, v2
    if a.sy > b.sy: a, b = b, a
    if b.sy > c.sy: b, c = c, b
    if a.sy > b.sy: a, b = b, a
    H = c.sy - a.sy
    if H <= 0 or H >= 0x8000:
        return None
    if (c.sy >> 4) < sc.y0 or (a.sy >> 4) > sc.y1:
        return None
    xs = sorted((a.sx, b.sx, c.sx))
    if (xs[0] >> 4) > sc.x1 or (xs[2] >> 4) <= sc.x0:
        return None
    if (xs[2] >> 4) - (xs[0] >> 4) >= 0x8000:
        return None
    s = {}
    s['dl'] = tdiv((c.sx - a.sx) << 12, H)                      # long edge, per 1/16 px
    s['du'] = 0
    s['dd'] = 0
    s['tmid'] = 0
    if c.sy != b.sy:
        s['dd'] = tdiv((c.sx - b.sx) << 12, c.sy - b.sy)
    w = (a.sx - b.sx) << 4
    if b.sy != a.sy:
        s['du'] = tdiv((a.sx - b.sx) << 12, a.sy - b.sy)
        w = ((s['dl'] * (b.sy - a.sy)) >> 8) + w
        w = s32(w)
        if w == 0:
            return None
        s['tmid'] = tdiv((b.sy - a.sy) << 16, H)
    if w == 0:
        return None
    s['W'] = w
    s['rW'] = tdiv(1 << 30, w)
    P = 15 - (a.sy & 15)
    if sc.y0 - (a.sy >> 4) > 0:
        P += (sc.y0 - (a.sy >> 4)) << 4
    s['P'] = P
    s['xl'] = s32((a.sx << 12) + s['dl'] * P)
    s['xu'] = s32((a.sx << 12) + s['du'] * P)
    s['xd'] = s32((b.sx << 12) + s['dd'] * (a.sy - b.sy + P))
    s['sl'], s['su'], s['sd'] = s['dl'] << 4, s['du'] << 4, s['dd'] << 4
    yb = min(c.sy >> 4, sc.y1)
    s['rows_lower'] = yb - (b.sy >> 4)
    ya = max(a.sy >> 4, sc.y0)
    s['rows'] = yb - ya
    if s['rows'] <= 0:
        return None
    s['row0'] = ya
    s['H'] = H
    s['x2mx0'] = c.sx - a.sx
    s['x0m16'] = a.sx - 16
    s['rH'] = tdiv(1 << 30, H)
    s['v'] = (a, b, c)
    return s


def gradients(s, attr):
    """affine gradients of a vertex attribute (0x4151d8..): (row start at x = 0 for the first row, d/dx, d/dy),
    all as attribute x 4096"""
    a, b, c = s['v']
    a0, a1, a2 = getattr(a, attr), getattr(b, attr), getattr(c, attr)
    e = s32((a2 - a0) * s['tmid'] + ((a0 - a1) << 16))
    gx = s32((e * s['rW']) >> 26)
    t = s32((gx * s['x2mx0']) >> 8)
    t = s32(-(t - ((a2 - a0) << 8)))
    gy = s32((t * s['rH']) >> 22)
    acc = gy * s['P'] - gx * s['x0m16'] + ((a0 + 1) << 16)
    row = s32(acc >> 4)
    return row, gx, gy


def draw_tex_gouraud(buf, tex, shd, sc, umask=255, vmask=255, wlog=8, hlog=8):
    """rasterizer 0x414e5c (affine path): pixel = shd[level * 256 + texel], level = Gouraud shade / 16;
    u, v are fractions of the texture (4096 = its width / height: texel = acc >> (24 - wlog))"""
    def draw(v0, v1, v2):
        s = setup(v0, v1, v2, sc)
        if s is None:
            return
        ru, gxu, gyu = gradients(s, 'u')
        rvv, gxv, gyv = gradients(s, 'v')
        rs, gxs, gys = gradients(s, 'shade')
        xl, xu, xd = s['xl'], s['xu'], s['xd']
        right_long = s['W'] > 0
        y = s['row0']
        for left in range(s['rows'], 0, -1):
            short = xu if left > s['rows_lower'] else xd
            l, r = (short, xl) if right_long else (xl, short)
            ix0 = max(l >> 16, sc.x0)
            ix1 = min(r >> 16, sc.x1)
            if ix1 > ix0:
                cs = np.arange(ix0, ix1, dtype=np.int64)
                U = (gxu * cs + ru) & 0xffffffff
                V = (gxv * cs + rvv) & 0xffffffff
                S = (gxs * cs + rs) & 0xffffffff
                ui = (U >> (24 - wlog)) & umask
                vi = ((V >> (24 - hlog)) & vmask) << wlog
                lev = (S >> 16) & 0xff
                texel = tex[ui + vi]
                buf[y, ix0:ix1] = shd[(lev << 8) + texel]
            xl, xu, xd = s32(xl + s['sl']), s32(xu + s['su']), s32(xd + s['sd'])
            ru, rvv, rs = s32(ru + gyu), s32(rvv + gyv), s32(rs + gys)
            y += 1
    return draw


# ------------------------------------------------------------------------------------------------ 2D bitmaps

def blit(buf, bmp_type, pixels, pitch, w, h, x, y, clip=(0, 0, 320, 200), table=None, key=0):
    """0x415df0 (0x415fe8 / 0x416174 / 0x4164ec / 0x416324): draw a w x h bitmap at (trunc(x), trunc(y)),
    clipped to the screen clip window (x0, y0, x1, y1).
    type 1: copy; 2: dst = table[src * 256 + dst]; 3: copy where src != key; 4: table blend where src != key."""
    cx0, cy0, cx1, cy1 = clip
    X0, Y0 = trunc(np.float32(x)), trunc(np.float32(y))
    X1, Y1 = trunc(np.float64(w) + np.float64(np.float32(x))), trunc(np.float64(h) + np.float64(np.float32(y)))
    if X0 >= cx1 or Y0 >= cy1 or X1 <= cx0 or Y1 <= cy0:
        return
    sx0, sy0 = max(cx0 - X0, 0), max(cy0 - Y0, 0)
    X0, Y0 = max(X0, cx0), max(Y0, cy0)
    X1, Y1 = min(X1, cx1), min(Y1, cy1)
    if X1 <= X0 or Y1 <= Y0:
        return
    src = np.frombuffer(pixels, np.uint8)
    for r in range(Y1 - Y0):
        o = (sy0 + r) * pitch + sx0
        s = src[o:o + X1 - X0].astype(np.int64)
        d = buf[Y0 + r, X0:X1]
        if bmp_type == 1:
            d[:] = s
        elif bmp_type == 2:
            d[:] = table[s * 256 + d]
        elif bmp_type == 3:
            m = s != key
            d[m] = s[m]
        else:
            m = s != key
            d[m] = table[s[m] * 256 + d[m]]
