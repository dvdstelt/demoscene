"""Python model of BARTi's 3D engine in STARS.EXE (0x7871c..0x7a210 + the fillers 0x4d2ee..0x55835).

Integer port of the 32-bit fixed-point code, written to reproduce the engine's output byte for byte from a memory
image (an emulator snapshot or a dump made by run_engine.py --engine-dump). Everything here is read from that
memory; nothing from the original is stored in this file. See ../../notes-engine.md for the description.

    m = Mem(bytearray(memory))      # memory as the program sees it (near pointer = index)
    draw_list(m, list_struct)       # 0x79483: camera, object culling and sorting, then 0x789c4 per object
    draw_object(m, obj)             # 0x789c4 with [0x582cd] as it is (a part's direct call)
The screen buffer [0x558d0] (selector) in m is drawn into, like the original.
"""
import struct


def s16(v): v &= 0xffff; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xffffffff; return v - 0x100000000 if v & 0x80000000 else v
def s8(v): v &= 0xff; return v - 0x100 if v & 0x80 else v
def u16(v): return v & 0xffff
def u32(v): return v & 0xffffffff


def idiv(a, b):
    """x86 idiv: truncation toward zero."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def rol16(v): v &= 0xffffffff; return ((v << 16) | (v >> 16)) & 0xffffffff


GDT = 0x91000            # where run_stars.py keeps the descriptors


class Mem:
    def __init__(self, data):
        self.b = data if isinstance(data, bytearray) else bytearray(data)

    def u8(self, a): return self.b[a]
    def u16(self, a): return self.b[a] | self.b[a + 1] << 8
    def i16(self, a): return s16(self.u16(a))
    def u32(self, a): return struct.unpack_from('<I', self.b, a)[0]
    def i32(self, a): return struct.unpack_from('<i', self.b, a)[0]
    def w8(self, a, v): self.b[a] = v & 0xff
    def w16(self, a, v): struct.pack_into('<H', self.b, a, v & 0xffff)
    def w32(self, a, v): struct.pack_into('<I', self.b, a, v & 0xffffffff)

    def selbase(self, sel):
        d = self.b[GDT + (sel & ~7):GDT + (sel & ~7) + 8]
        return d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24


# ----------------------------------------------------------------------------------------------------------------
# matrices (0x7984e, 0x79d25): 4 rows of 4 dwords, 1024 = 1.0; row 3 = translation; v' = v . M (row vector)

COS = 0x5599c            # 4096 words: cos table, 1023 = 1.0 (static data in the image)


def cos_(m, a): return m.i16(COS + 2 * (a & 0xfff))
def sin_(m, a): return m.i16(COS + 2 * ((a - 0x400) & 0xfff))     # cos(a - 90 deg)


def sar(v, n): return v >> n            # Python >> on ints is arithmetic


def mul10(a, b): return s32((a * b) >> 10)     # imul + shrd 10 (low 32 bits of the 64-bit product >> 10)


def build_matrix(m, pos, ang):
    """0x7984e: esi = struct {int x, y, z; short a, b, c}. M = Rx(a) Ry(b) Rz(c) in column form, stored as rows."""
    a, b, c = ang
    ca, sa = cos_(m, a), sin_(m, a)
    cb, sb = cos_(m, b), sin_(m, b)
    cc, sc = cos_(m, c), sin_(m, c)
    M = [[0] * 4 for _ in range(4)]
    M[0][0] = mul10(cc, cb)
    M[0][1] = mul10(-sc, cb)
    M[0][2] = sb
    M[1][0] = mul10(sc, ca) + mul10(mul10(sb, cc), sa)
    M[1][1] = s32(mul10(cc, ca) - mul10(mul10(sa, sb), sc))
    M[1][2] = mul10(-sa, cb)
    M[2][0] = s32(mul10(sc, sa) - mul10(mul10(sb, cc), ca))
    M[2][1] = s32(mul10(cc, sa) + mul10(mul10(ca, sb), sc))
    M[2][2] = mul10(ca, cb)
    M[3][0], M[3][1], M[3][2] = pos
    return M


def read_matrix(m, a):
    return [[m.i32(a + 16 * r + 4 * c) for c in range(4)] for r in range(4)]


def write_matrix(m, a, M):
    for r in range(4):
        for c in range(4):
            m.w32(a + 16 * r + 4 * c, M[r][c])


def write_built(m, a, M):
    """0x7984e writes only the 3x3 part and the translation (+0x30..+0x38)."""
    for r in range(3):
        for c in range(3):
            m.w32(a + 16 * r + 4 * c, M[r][c])
    for c in range(3):
        m.w32(a + 0x30 + 4 * c, M[3][c])


def matmul(A, B):
    """0x79d25: out[i][j] = sum_k (A[i][k] * B[k][j]) >> 10 over all 4x4 (column 3 / row 3 as they are)."""
    return [[s32(sum(mul10(A[i][k], B[k][j]) for k in range(4))) for j in range(4)] for i in range(4)]


# ----------------------------------------------------------------------------------------------------------------
# engine state addresses

CAM = 0x55836            # camera struct: int x, y, z; short a, b, c
ENVOFS = 0x55862         # short[3] added to the object angles for the normal (environment) matrix
NSTRUCT = 0x55876        # struct used for the normal matrix (pos unused, angles at 0x55882)
CAMM = 0x5799e           # camera matrix
COMB = 0x579de           # object x camera
OBJM = 0x57a1e           # object matrix
NORM = 0x57a5e           # normal matrix
F = 0x57ab6              # int: eye distance (projection)
F2 = 0x57aba             # int: the same for the alternative projection 0x79df3 / 0x79192
SHIFT = 0x5594c          # byte: projection shift
MODE = 0x55948           # int: render mode, row of the table 0x784d1 (0x15 bytes per row)
NOSORT = 0x582ac         # byte: 1 = do not sort faces
ALTPROJ = 0x5799c        # byte: 1 = projection 0x79df3
USECAM = 0x582cd         # byte: set by 0x79483 while it draws its objects
UVSHIFT = 0x582cf        # byte: texture uv word << this (prep 0x4d566 / 0x4d9d1 / 0x4da28)
MATERIALS = 0x582a6      # dword: material table (16 bytes per material: +0 ptr, +4 selector)
CUROBJ = 0x582ad         # object struct the scene list fills for each object it draws

P_FACELIST, P_PROJ, P_ROT, P_VNORM, P_FNORM, P_PREP, P_OBJLIST = 0x558f0, 0x558e4, 0x558ea, 0x558fc, 0x55902, \
    0x55908, 0x5591a


class Engine:
    def __init__(self, m):
        self.m = m
        self.log = []

    # ---- object set-up 0x78a35 and transform 0x79b16 / 0x79df3
    def setup(self, obj):
        m = self.m
        data = m.u32(obj + 0x12)
        self.nv, self.nf = m.u16(data), m.u16(data + 2)
        self.verts = data + 4
        self.faces = data + 4 + 16 * self.nv
        ang = [s16(m.u16(obj + 0xc + 2 * i) + m.u16(ENVOFS + 2 * i)) for i in range(3)]
        for i in range(3):
            m.w16(0x55882 + 2 * i, ang[i])
        A = build_matrix(m, [m.i32(obj + 4 * i) for i in range(3)], [m.i16(obj + 0xc + 2 * i) for i in range(3)])
        write_built(m, OBJM, A)
        Nm = build_matrix(m, [m.i32(NSTRUCT + 4 * i) for i in range(3)], ang)
        write_built(m, NORM, Nm)
        C = matmul(read_matrix(m, OBJM), read_matrix(m, CAMM))
        write_matrix(m, COMB, C)
        self.M = C if m.u8(USECAM) else read_matrix(m, OBJM)
        self.N = read_matrix(m, NORM)

    def transform(self):
        m, M = self.m, self.M
        rot, proj = m.u32(P_ROT), m.u32(P_PROJ)
        alt = m.u8(ALTPROJ) == 1
        f = m.i32(F2 if alt else F)
        sh = m.u8(SHIFT) & 31
        for i in range(self.nv):
            v = self.verts + 16 * i
            x, y, z = m.i16(v), m.i16(v + 2), m.i16(v + 4)
            X = s32(x * M[0][0] + y * M[1][0] + M[3][0] + z * M[2][0])
            Y = s32(x * M[0][1] + y * M[1][1] + M[3][1] + z * M[2][1])
            Z = s32(x * M[0][2] + y * M[1][2] + M[3][2] + z * M[2][2])
            m.w32(rot + 16 * i, X); m.w32(rot + 16 * i + 4, Y); m.w32(rot + 16 * i + 8, Z)
            d = s32(f - (Z >> 10))
            if not alt:
                d = s32(d << sh)
            if d <= 0:
                sx, sy = X >> 10, Y >> 10
            else:
                if alt:
                    sx, sy = idiv(X >> 1, d) + 0xa0, idiv(Y >> 1, d) + 0x64
                else:
                    sx, sy = idiv(X, d) + 0xa0, idiv(Y, d) + 0x64
            m.w16(proj + 4 * i, sx); m.w16(proj + 4 * i + 2, sy)

    def normals(self, which):
        """0x79a17 with 0x789f5 (face normals -> [0x55902]) or 0x78a15 (vertex normals -> [0x558fc])."""
        m, N = self.m, self.N
        if which == 'face':
            src, cnt, out = self.faces + 8, self.nf, m.u32(P_FNORM)
        else:
            src, cnt, out = self.verts + 6, self.nv, m.u32(P_VNORM)
        for i in range(cnt):
            a = src + 16 * i
            x, y, z = m.i16(a), m.i16(a + 2), m.i16(a + 4)
            for c in range(3):
                m.w32(out + 16 * i + 4 * c, x * N[0][c] + y * N[1][c] + z * N[2][c])

    # ---- 0x78b08: back-face culling, depth range, face list, sort
    def facelist(self):
        m = self.m
        rot, proj = m.u32(P_ROT), m.u32(P_PROJ)
        lst = m.u32(P_FACELIST)
        near = s32(-(m.i32(F) << 10))
        n = 0
        for i in range(self.nf):
            fa = self.faces + 16 * i
            a, b, c = m.u16(fa + 2), m.u16(fa + 4), m.u16(fa + 6)
            z = m.i32(rot + 16 * a + 8)
            z = (z + m.i32(rot + 16 * b + 8)) >> 1
            z = (z + m.i32(rot + 16 * c + 8)) >> 1
            xa, ya = m.u16(proj + 4 * a), m.u16(proj + 4 * a + 2)
            dxb, dyb = s16(m.u16(proj + 4 * b) - xa), s16(m.u16(proj + 4 * b + 2) - ya)
            dxc, dyc = s16(m.u16(proj + 4 * c) - xa), s16(m.u16(proj + 4 * c + 2) - ya)
            cross = s32(dxb * dyc - dxc * dyb)
            if cross <= 0 or z >= near or z <= s32(0xfaa2b580):
                continue
            m.w32(lst + 4 * n, (i << 16) | (((z << 6) >> 16) & 0xffff))
            n += 1
        m.w32(0x55928, n)
        m.w16(0x4d224, self.nf)
        self.nfl = n
        flag = m.u8(NOSORT)
        if flag != 1 and n - 1 > 0 and not flag & 1:
            qsort(m, lst, 0, (n - 1) * 4)

    # ---- per-mode preparation (4th entry of the mode table)
    def prep(self, addr):
        m = self.m
        out = m.u32(P_PREP)
        vn = m.u32(P_VNORM)
        if addr in (0x4d4b4, 0x4d4e7):                 # env (u, v) words: n >> 12 / n >> 11, + 0x80
            sh = 12 if addr == 0x4d4b4 else 11
            for i in range(self.nv):
                u = ((m.i32(vn + 16 * i) >> sh) + 0x80) & 0xff
                v = ((m.i32(vn + 16 * i + 4) >> sh) + 0x80) & 0xff
                m.w16(out + 2 * i, u | v << 8)
        elif addr in (0x4da28, 0x4d566, 0x4d9d1):      # dword: lo = texture uv << [0x582cf], hi = env uv
            sh = 12 if addr != 0x4d566 else 11
            cl = m.u8(UVSHIFT) & 31
            add = m.u16(0x4d2ec) if addr == 0x4d9d1 else 0
            for i in range(self.nv):
                u = ((m.i32(vn + 16 * i) >> sh) + 0x80) & 0xff
                v = ((m.i32(vn + 16 * i + 4) >> sh) + 0x80) & 0xff
                t = ((m.u16(self.verts + 16 * i + 0xc) << cl) + add) & 0xffff
                m.w32(out + 4 * i, t | (u | v << 8) << 16)
        elif addr == 0x789f4:
            pass
        else:
            raise NotImplementedError('prep %x' % addr)

    # ---- drawing (5th entry)
    def draw(self, addr):
        m = self.m
        n = m.i32(0x55928)
        if n <= 0:
            return
        lst, proj, prep = m.u32(P_FACELIST), m.u32(P_PROJ), m.u32(P_PREP)
        self.screen = m.selbase(m.u16(0x558d0))
        if addr == 0x4d5af:                             # modes 2, 3, 8?: 0x4e710 with the env map
            m.w16(0x55912, m.u16(0x55918))
        for k in range(n):
            fi = m.u32(lst + 4 * k) >> 16
            fa = self.faces + 16 * fi
            ia, ib, ic = m.u16(fa + 2), m.u16(fa + 4), m.u16(fa + 6)
            A, B, C = m.u32(proj + 4 * ia), m.u32(proj + 4 * ib), m.u32(proj + 4 * ic)
            if addr == 0x4d5af:
                pa, pb, pc = m.u32(prep + 2 * ia), m.u32(prep + 2 * ib), m.u32(prep + 2 * ic)
                fill2(m, A, B, C, pa, pb, pc, 'env', self.screen)
            elif addr in (0x4dbd7,):
                mat = m.u32(MATERIALS)
                m.w16(0x55912, m.u16(mat + 4))           # material 0 (the face's own index is cleared)
                pa, pb, pc = m.u32(prep + 4 * ia), m.u32(prep + 4 * ib), m.u32(prep + 4 * ic)
                fill4(m, A, B, C, pa, pb, pc, 'texbump', self.screen)
            elif addr in (0x4ddbf, 0x4deb1):
                pa, pb, pc = m.u32(prep + 4 * ia), m.u32(prep + 4 * ib), m.u32(prep + 4 * ic)
                wide = addr == 0x4ddbf
                if m.u16(fa) == 1:
                    fill2(m, A, B, C, rol16(pa), rol16(pb), rol16(pc), 'env', self.screen, wide)
                else:
                    fill4(m, A, B, C, pa, pb, pc, 'bump', self.screen, wide)
            else:
                raise NotImplementedError('draw %x' % addr)


def qsort(m, base, lo, hi):
    """0x7898d: quicksort of dwords [base+lo .. base+hi] by their signed low word, first element as pivot."""
    while hi > lo:
        p = lo
        piv = m.u32(base + lo)
        i = lo + 4
        while True:
            e = m.u32(base + i)
            if s16(piv) > s16(e):
                m.w32(base + p, e)
                p += 4
                m.w32(base + i, m.u32(base + p))
                m.w32(base + p, piv)
            i = (i & ~0xffff) | ((i + 4) & 0xffff)
            if i > hi:
                break
        qsort(m, base, lo, p)
        lo = (lo & ~0xffff) | ((p + 4) & 0xffff)


# ----------------------------------------------------------------------------------------------------------------
# triangle fillers. Shared state at 0x4d190..0x4d256 lives in memory (stale values matter, as in the original).

V = 0x4d210              # A, B, C, A, B as dwords (x lo, y hi)
PRM = 0x4d1fc            # their parameters (A, B, C, A, B)
# edge records: 02 at 0x4d1b4 (dx), 0x4d1b8 (dy), 0x4d1bc.. (param steps); 01 at 0x4d1cc; 12 at 0x4d1e4
E02, E01, E12 = 0x4d1b4, 0x4d1cc, 0x4d1e4


def setup_tri(m, A, B, C, pa, pb, pc, width, ytop_max):
    """Shared start of every filler. Returns (edi, clipped) or None."""
    for i, p in enumerate((pa, pb, pc, pa, pb)):
        m.w32(PRM + 4 * i, p)
    lim = width
    nin = sum(1 for v in (A, B, C) if (v & 0xffff) < lim)
    if nin == 0:
        return None
    clipped = nin != 3
    for i, v in enumerate((A, B, C, A, B)):
        m.w32(V + 4 * i, v)
    a, b, c = s32(A), s32(B), s32(C)
    edi = 0
    lo = min(a, b)
    top = lo if c > lo else c
    if top == c:
        edi = 8
    elif top == b:
        edi = 4
    if top >= ytop_max << 16:
        return None
    return edi, clipped


def edge(m, edi, ia, ib, rec, nparam, skip1):
    """One edge from vertex slot ia to ib (offsets 0/4/8 relative to edi). rec = address of its dx dword."""
    ya, yb = m.u16(V + edi + ia + 2), m.u16(V + edi + ib + 2)
    cy = s16(yb - ya)
    if cy != 0:
        if cy != -1 and not (skip1 and cy == 1):
            pa, pb = m.u32(PRM + edi + ia), m.u32(PRM + edi + ib)
            if nparam == 4:
                d = u32(pb - pa)                       # one 32-bit subtraction: borrows run into the next byte
                bytes_ = [(d >> (8 * k)) & 0xff for k in range(4)]
            else:
                bytes_ = [((pb >> (8 * k)) - (pa >> (8 * k))) & 0xff for k in range(2)]
            for k in range(nparam):
                m.w16(rec + 8 + 2 * k, idiv(s16(bytes_[k] << 8), cy))
        xa, xb = m.u16(V + edi + ia), m.u16(V + edi + ib)
        m.w32(rec, idiv(s32(u16(xb - xa) << 16), cy))
    m.w16(rec + 4, cy)
    return cy


def mid_grad(m, edi, mid, longrec, nparam, skip1):
    """0x4eac9 (2 params) / 0x4ff31 (4 params): params and x of the long edge at the middle vertex's row and the
    span gradients d/dx from there to the middle vertex."""
    dy = s16(m.u16(V + edi + mid + 2) - m.u16(V + edi + 2))
    dx = m.i32(longrec)
    steps = [m.i16(longrec + 8 + 2 * k) for k in range(nparam)]
    x = s32(dx * dy + (m.u16(V + edi) << 16))
    m.w32(0x4d1b0, x)
    p0 = m.u32(PRM + edi)
    vals = []
    for k in range(nparam):
        vals.append(u16(steps[k] * dy + (((p0 >> (8 * k)) & 0xff) << 8)))
    m.w16(0x4d228, vals[0]); m.w16(0x4d22a, vals[1])
    if nparam == 4:
        m.w16(0x4d234, vals[2]); m.w16(0x4d236, vals[3])
    cx = s16(m.u16(V + edi + mid) - (x >> 16))
    if cx == 0 or cx == -1 or (skip1 and cx == 1):
        return
    pm = m.u32(PRM + edi + mid)
    for k in range(nparam):
        m.w16(0x4d240 + 2 * k, idiv(s16((((pm >> (8 * k)) & 0xff) << 8) - vals[k]), cx))


# per filler: pitch, top-vertex reject (y >= ytop), P1/P2 reject (both y < lim), span: y >= yret returns,
# first row ylo, last row ylast
FILLERS = {
    ('env', True): (320, 199, 1, 199, 1, 197),          # 0x4e710
    ('texbump', True): (320, 199, 1, 199, 1, 197),      # 0x51593
    ('bump', True): (320, 200, 0, 200, 0, 198),         # 0x4f47c
    ('bump', False): (256, 255, 0, 255, 0, 253),        # 0x54548
    ('env', False): (256, 200, 0, 255, 0, 254),         # 0x52e96
}


class Filler:
    """kind: 'env' (2 params, 0x4e710 / 0x52e96), 'texbump' (4, 0x51593), 'bump' (4, 0x4f47c / 0x54548)."""

    def __init__(self, m, kind, screen, wide=True):
        self.m, self.kind, self.screen = m, kind, screen
        self.pitch, self.ytop, self.lim, self.yret, self.ylo, self.ylast = FILLERS[(kind, wide)]
        self.nparam = 2 if kind == 'env' else 4
        self.skip1 = self.nparam == 4


USED = {}               # (mode, filler kind, 320 wide) -> faces drawn, for the tests


def fill2(m, A, B, C, pa, pb, pc, kind, screen, wide=True):
    USED[(m.i32(MODE), kind, wide)] = USED.get((m.i32(MODE), kind, wide), 0) + 1
    _fill(m, A, B, C, pa, pb, pc, Filler(m, kind, screen, wide))


def fill4(m, A, B, C, pa, pb, pc, kind, screen, wide=True):
    USED[(m.i32(MODE), kind, wide)] = USED.get((m.i32(MODE), kind, wide), 0) + 1
    _fill(m, A, B, C, pa, pb, pc, Filler(m, kind, screen, wide))


def _fill(m, A, B, C, pa, pb, pc, f):
    m.w32(0x4d240, 0)
    if f.nparam == 4:
        m.w32(0x4d244, 0)
    r = setup_tri(m, A, B, C, pa, pb, pc, f.pitch, f.ytop)
    if r is None:
        return
    edi, clipped = r
    if m.i16(V + edi + 6) < f.lim and m.i16(V + edi + 10) < f.lim:
        return
    np_, s1 = f.nparam, f.skip1
    edge(m, edi, 0, 8, E02, np_, s1)
    edge(m, edi, 0, 4, E01, np_, s1)
    dy12 = edge(m, edi, 4, 8, E12, np_, s1)
    P0 = m.u32(PRM + edi)
    x0 = m.u16(V + edi)
    def init_params(p, keep_cl=0):
        return [((p >> (8 * k)) & 0xff) << 8 | keep_cl for k in range(np_)]
    if dy12 >= 0:
        mid_grad(m, edi, 4, E02, np_, s1)
        L = R = x0 << 16
        span(m, f, clipped, L, R, init_params(P0), m.i16(V + edi + 2), m.i16(E01 + 4),
             m.i32(E02), [m.i16(E02 + 8 + 2 * k) for k in range(np_)], m.i32(E01))
        L = m.u32(0x4d1b0)
        R = m.u16(V + edi + 4) << 16
        prm = [m.u16(0x4d228), m.u16(0x4d22a)] + ([m.u16(0x4d234), m.u16(0x4d236)] if np_ == 4 else [])
        span(m, f, clipped, L, R, prm, m.i16(V + edi + 6), dy12,
             m.i32(E02), [m.i16(E02 + 8 + 2 * k) for k in range(np_)], m.i32(E12))
    else:
        mid_grad(m, edi, 8, E01, np_, s1)
        L = R = x0 << 16
        span(m, f, clipped, L, R, init_params(P0), m.i16(V + edi + 2), m.i16(E02 + 4),
             m.i32(E02), [m.i16(E02 + 8 + 2 * k) for k in range(np_)], m.i32(E01))
        L = m.u16(V + edi + 8) << 16
        R = m.u32(0x4d1b0)
        P2 = m.u32(PRM + edi + 8)
        cl = 0 if np_ == 4 else (m.u32(0x4d1b0) & 0xff)   # 0x4e710 leaves the low byte of the long edge's x in cl
        span(m, f, clipped, L, R, init_params(P2, cl), m.i16(V + edi + 10), s16(-dy12),
             m.i32(E12), [m.i16(E12 + 8 + 2 * k) for k in range(np_)], m.i32(E01))


def span(m, f, clipped, L, R, prm, y, count, dxl, dprm, dxr):
    """The span routine (0x4eb93 / 0x4ed30 / 0x51da8 / 0x51a62 / 0x4f94b / 0x4fbe0 ...)."""
    if count <= 0:
        return
    np_ = f.nparam
    ddx = [m.i16(0x4d240 + 2 * k) for k in range(np_)]
    pitch = f.pitch
    if y >= f.yret:
        return
    prm = [p & 0xffff for p in prm]
    if y < f.ylo:
        n = f.ylo - y
        if n >= count:
            return
        L, R = u32(L + dxl * n), u32(R + dxr * n)
        prm = [u16(prm[k] + dprm[k] * n) for k in range(np_)]
        count -= n
        y = f.ylo
    if y + count > f.ylast + 1:
        count = f.ylast + 1 - y
        if count <= 0:
            return
    rowaddr = y * pitch
    L, R = u32(L + (rowaddr << 16)), u32(R + (rowaddr << 16))
    stepL, stepR = u32(dxl + (pitch << 16)), u32(dxr + (pitch << 16))
    scr = f.screen
    dw = f.kind == 'texbump'      # 0x51593 steps the params per row with dword adds (carry u -> v); the others by word
    for _ in range(count):
        x0, x1 = L >> 16, R >> 16
        p = list(prm)
        if clipped:
            if x1 >= rowaddr:
                if x0 < rowaddr:
                    k = s16(x0 - rowaddr)
                    p = [u16(p[j] - k * ddx[j]) for j in range(np_)]
                    x0 = rowaddr
                end = rowaddr + pitch
                if x0 < end:
                    if x1 >= end:
                        x1 = end
                    if s16(x1 - x0) > 0:
                        pixels(m, f, scr, x0, s16(x1 - x0), p, ddx)
        else:
            n = s16(x1 - x0)
            if n > 0:
                pixels(m, f, scr, x0, n, p, ddx)
        L, R = u32(L + stepL), u32(R + stepR)
        if clipped:
            rowaddr += pitch
        if dw:
            t = u32((prm[0] | prm[1] << 16) + (dprm[0] & 0xffff | (dprm[1] & 0xffff) << 16))
            e = u32((prm[2] | prm[3] << 16) + (dprm[2] & 0xffff | (dprm[3] & 0xffff) << 16))
            prm = [t & 0xffff, t >> 16, e & 0xffff, e >> 16]
        else:
            prm = [u16(prm[k] + dprm[k]) for k in range(np_)]


def pixels(m, f, scr, x, n, p, d):
    b = m.b
    if f.kind == 'env':
        base = m.selbase(m.u16(0x55912))
        u, v = p[0], p[1]
        du, dv = d[0] & 0xffff, d[1] & 0xffff
        for i in range(n):
            b[scr + ((x + i) & 0xffff)] = b[base + ((v & 0xff00) | (u >> 8 & 0xff))]
            v = (v + dv) & 0xffff
            u = (u + du) & 0xffff
        return
    env = m.selbase(m.u16(0x55918))
    # register images as the loops keep them: ecx = u_e:u_t, edx = v_e:v_t ("tex orientation")
    ecx = p[0] | p[2] << 16
    edx = p[1] | p[3] << 16
    DU = (d[0] & 0xffff) | (d[2] & 0xffff) << 16
    DV = (d[1] & 0xffff) | (d[3] & 0xffff) << 16
    if f.kind == 'texbump':
        tex = m.selbase(m.u16(0x55912))
        bump = m.selbase(m.u16(0x558dc))
        def px_byte(ecx, edx):
            bx = ((edx >> 8) & 0xff) << 8 | ((ecx >> 8) & 0xff)
            al = b[tex + bx]
            w = b[bump + bx] | b[bump + bx + 1] << 8
            hi = ((w >> 8) + ((edx >> 24) & 0xff)) & 0xff
            lo = ((w & 0xff) + ((ecx >> 24) & 0xff)) & 0xff
            return (al + b[env + (hi << 8 | lo)]) & 0xff
        def px_word(ecx, edx):
            # second pixel of a pair: env coords as a word plus the bump word (carry from u into v)
            ebx = ((edx >> 24) & 0xff) << 8 | ((ecx >> 24) & 0xff)
            bx = ((edx >> 8) & 0xff) << 8 | ((ecx >> 8) & 0xff)
            al = b[tex + bx]
            w = b[bump + bx] | b[bump + bx + 1] << 8
            return (al + b[env + ((ebx + w) & 0xffff)]) & 0xff
    else:
        bump = m.selbase(m.u16(0x558dc))
        def px_byte(ecx, edx):
            bx = ((edx >> 8) & 0xff) << 8 | ((((ecx >> 8) & 0xff) * 2) & 0xff)
            w = b[bump + bx] | b[bump + bx + 1] << 8
            hi = ((w >> 8) + ((edx >> 24) & 0xff)) & 0xff
            lo = ((w & 0xff) + ((ecx >> 24) & 0xff)) & 0xff
            return b[env + (hi << 8 | lo)]
        def px_word(ecx, edx):
            ebx = ((edx >> 24) & 0xff) << 8 | ((ecx >> 24) & 0xff)
            bx = ((edx >> 8) & 0xff) << 8 | ((((ecx >> 8) & 0xff) * 2) & 0xff)
            w = b[bump + bx] | b[bump + bx + 1] << 8
            return b[env + ((ebx + w) & 0xffff)]
    a = x
    if a & 1:
        b[scr + (a & 0xffff)] = px_byte(ecx, edx)
        ecx, edx = u32(ecx + DU), u32(edx + DV)
        a += 1; n -= 1
        if n == 0:
            return
    DUr, DVr = rol16(DU), rol16(DV)
    for _ in range(n >> 1):
        b[scr + (a & 0xffff)] = px_byte(ecx, edx)
        # the first pixel's step is added with the registers in env orientation
        ecx, edx = rol16(u32(rol16(ecx) + DUr)), rol16(u32(rol16(edx) + DVr))
        b[scr + ((a + 1) & 0xffff)] = px_word(ecx, edx)
        ecx, edx = u32(ecx + DU), u32(edx + DV)
        a += 2
    if n & 1:
        b[scr + (a & 0xffff)] = px_byte(ecx, edx)


# ----------------------------------------------------------------------------------------------------------------
# 0x789c4 and 0x79483

MODE_TABLE = 0x784d1


def draw_object(m, obj):
    e = Engine(m)
    mode = m.i32(MODE)
    row = MODE_TABLE + 0x15 * mode
    fn = [m.u32(row + 4 * i) for i in range(5)]
    assert fn[0] == 0x78a35 and fn[2] == 0x78b08
    e.setup(obj)
    e.transform()
    if fn[1] == 0x789f5:
        e.normals('face')
    elif fn[1] == 0x78a15:
        e.normals('vertex')
    e.facelist()
    e.prep(fn[3])
    e.draw(fn[4])
    return e


def draw_list(m, lst):
    """0x79483: lst = {int *objects (-1 ends), work (16 bytes per object), capacity, bytes used}."""
    m.w8(USECAM, 0)
    cam = [m.i32(CAM + 4 * i) for i in range(3)]
    B = build_matrix(m, cam, [m.i16(CAM + 0xc + 2 * i) for i in range(3)])
    write_built(m, CAMM, B)
    B = read_matrix(m, CAMM)
    objs, work = m.u32(lst), m.u32(lst + 4)
    f, sh = m.i32(F), m.u8(SHIFT) & 31
    i = 0
    while True:
        p = m.u32(objs + 4 * i)
        if p == 0xffffffff:
            break
        r = m.i32(p + 0x1a)
        d = [s32(m.i32(p + 4 * k) - cam[k]) for k in range(3)]
        X = s32(d[0] * B[0][0] + d[1] * B[1][0] + d[2] * B[2][0])
        Y = s32(d[0] * B[0][1] + d[1] * B[1][1] + d[2] * B[2][1])
        Z = s32(d[0] * B[0][2] + d[1] * B[1][2] + d[2] * B[2][2])
        w = work + 16 * i
        m.w32(w, X); m.w32(w + 4, Y); m.w32(w + 8, Z)
        D = s32(s32(f - (Z >> 10)) << sh)
        if D <= 0:
            sx, sy, sr = s16(X >> 10), s16(Y >> 10), s16(r)
        else:
            sx, sy, sr = s16(idiv(X, D) + 0xa0), s16(idiv(Y, D) + 0x64), s16(idiv(s32(r << 10), D))
        vis = (s16(sx + sr) > 0 and s16(sx - sr) < 0x140 and s16(sy + sr) > 0 and s16(sy - sr) < 0xc8
               and s32((Z >> 10) - r) < -f and Z > s32(0xff830000))
        m.w32(w + 12, 0 if vis else 0xff)
        i += 1
    m.w32(work + 16 * i, 0xffffffff)
    sl = m.u32(P_OBJLIST)
    n = 0
    j = 0
    while m.u32(work + 16 * j) != 0xffffffff:
        if m.u8(work + 16 * j + 12) == 0:
            m.w32(sl + 4 * n, (j << 16) | ((m.i32(work + 16 * j + 8) >> 10) & 0xffff))
            n += 1
        j += 1
    if n == 0:
        return
    if n > 1:
        qsort(m, sl, 0, (n - 1) * 4)
    for k in range(4):
        m.w32(0x579ce + 4 * k, 0)
    m.w8(USECAM, 1)
    for k in range(n):
        j = m.u32(sl + 4 * k) >> 16
        p = m.u32(objs + 4 * j)
        for c in range(3):
            m.w32(CUROBJ + 4 * c, (m.i32(p + 4 * c) - cam[c]) << 10)
        for c in range(3):
            m.w16(CUROBJ + 0xc + 2 * c, m.u16(p + 0xc + 2 * c))
        m.w32(CUROBJ + 0x12, m.u32(p + 0x12))
        m.w32(CUROBJ + 0x1a, m.u32(p + 0x1a))
        m.w32(CUROBJ + 0x16, m.u32(p + 0x16))
        draw_object(m, CUROBJ)
    m.w8(USECAM, 0)
