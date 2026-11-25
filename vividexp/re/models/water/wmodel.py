"""Python model of WaterEffect 0x4064b0: object placement from the splines, rotation, projection, environment
coordinates, back-face flags, face sorting, the bump-environment rasterizer 0x414f13 and the affine texture
rasterizer 0x41448a (both exact integer models), and a whole frame.

    import wdata, wmodel
    D = wdata.load()
    frame = wmodel.render(D, times)   # times[k] = refreshes since WaterEffect started, per object k (0 = transparent)
"""
import numpy as np

from wdata import s16, s32, idiv, fsin, fcos

SPLINE_SPEED_STEP = 0x10000 // 128      # [0x833dc4] = 65536 / SplineSpeed = 512 per refresh
SPACING = 95000                         # ObjectSpacing; object k (1..3) has offset (k - 1) * spacing, object 0 none


def obj_t(D, k, count):
    off = 0 if k == 0 else (k - 1) * int(D['ini']['ObjectSpacing'])
    return s32(count * (0x10000 // int(D['ini']['SplineSpeed'])) + off)


def place(D, k, t):
    """the six spline values >> 8: x, y, z, angle a (about X), b (about Y), c (about Z)"""
    import wdata
    chans = D['trans'] if k == 0 else D['solid']
    return [wdata.spline_eval(ch, t) >> 8 for ch in chans]


class Obj:
    pass


def transform(D, k, pv):
    """0x413c93 reset, 0x413d98 Y rotation by b, 0x413e6a X rotation by a, 0x413f37 Z rotation by c,
    0x414160 environment coordinates. Returns Obj with v[i] = [bu, bv, sx, sy, x, y, z] and n[i] = [eu, ev, nz]."""
    m = D['objs'][k]
    tab = D['sin']
    x0, y0, z0, a, b, c = pv
    o = Obj()
    o.k = k
    o.pv = pv
    V = [[x, y, z] for (x, y, z) in m['pos']]
    N = [[x, y, z] for (x, y, z) in m['nrm']]
    sa, ca = fsin(tab, a), fcos(tab, a)
    sb, cb = fsin(tab, b), fcos(tab, b)
    sc, cc = fsin(tab, c), fcos(tab, c)
    for L in (V, N):
        for p in L:                       # about Y (x, z)
            x, z = p[0], p[2]
            p[0] = s16((x * cb - z * sb) >> 16)
            p[2] = s16((z * cb + x * sb) >> 16)
    for L in (V, N):
        for p in L:                       # about X (y, z)
            y, z = p[1], p[2]
            p[1] = s16((y * ca - z * sa) >> 16)
            p[2] = s16((z * ca + y * sa) >> 16)
    for L in (V, N):
        for p in L:                       # about Z (x, y)
            x, y = p[0], p[1]
            p[1] = s16((y * cc - x * sc) >> 16)
            p[0] = s16((x * cc + y * sc) >> 16)
    et = D['envtab']
    o.V = V
    o.N = [[et[n[0] >> 6], et[n[1] >> 6], n[2]] for n in N]
    o.uv = m['uv']
    o.faces = m['faces']
    o.S = [None] * len(m['pos'])
    return o


def project(o, sx, sy, camz, px, py, pz, cx, cy):
    """0x414000: screen x, y of the vertices (not the centre vertex)"""
    kx = s32(sx * camz) >> 20
    ky = -(s32(sy * camz) >> 20)
    S = []
    for i in range(len(o.V) - 1):
        x, y, z = o.V[i]
        d = camz - (z + pz)
        if d == 0:
            S.append(o.S[i])
            continue
        Y = s16(idiv((y + py) * ky, d) + cy)
        X = s16(idiv((x + px) * kx, d) + cx)
        S.append((X, Y))
    o.S = S + [None]


def backface(o):
    """0x4140e0: flag 1 (skipped) when (x0 - x1)(y2 - y1) < (y0 - y1)(x2 - x1), 16-bit differences"""
    fl = []
    for f in o.faces:
        (x0, y0), (x1, y1), (x2, y2) = o.S[f[0]], o.S[f[1]], o.S[f[2]]
        A = s16(y0 - y1) * s16(x2 - x1)
        B = s16(x0 - x1) * s16(y2 - y1)
        fl.append(1 if B < A else 0)
    o.flags = fl


def visible_sorted(o):
    """0x411980 keys (min z + (max z - min z) / 2, 16 bit), 0x4119f0 insertion sort (< 200) or 0x411a30 heap sort,
    both farthest first"""
    keys = []
    for i, f in enumerate(o.faces):
        if o.flags[i]:
            continue
        zs = sorted([o.V[f[0]][2], o.V[f[1]][2], o.V[f[2]][2]])
        lo, hi = zs[0], zs[2]
        key = s16(lo + (s16(hi - lo) >> 1))
        keys.append((i, key))
    if len(keys) < 2:
        return [i for i, _ in keys]
    if len(keys) < 200:
        out = []
        for e in keys:                   # insertion: stop at the first earlier key >= this one
            j = len(out)
            while j > 0 and out[j - 1][1] < e[1]:
                j -= 1
            out.insert(j, e)
        return [i for i, _ in out]
    return [i for i, _ in heapsort(keys)]


def heapsort(a):
    """0x411a30: Numerical Recipes' hpsort with reversed comparisons (a min-heap: result descending), 16-bit keys"""
    n = len(a)
    ra = [None] + list(a)
    l = (n >> 1) + 1
    ir = n
    while True:
        if l > 1:
            l -= 1
            rra = ra[l]
        else:
            rra = ra[ir]
            ra[ir] = ra[1]
            ir -= 1
            if ir == 1:
                ra[1] = rra
                break
        i = l
        j = l + l
        while j <= ir:
            if j < ir and ra[j][1] > ra[j + 1][1]:
                j += 1
            if rra[1] > ra[j][1]:
                ra[i] = ra[j]
                i = j
                j <<= 1
            else:
                j = ir + 1
        ra[i] = rra
    return ra[1:]


# ---------------------------------------------------------------------------------------------------------------
# rasterizers

class Target:
    def __init__(self, buf, pitch, xmin, ymin, xmax, ymax):
        self.buf = buf                   # flat uint8 array
        self.pitch, self.xmin, self.ymin, self.xmax, self.ymax = pitch, xmin, ymin, xmax, ymax


def _sort3(d, s, e):
    if d[1] > s[1]:
        d, s = s, d
    if d[1] > e[1]:
        d, e = e, d
    if s[1] > e[1]:
        s, e = e, s
    return d, s, e


def _div16(a, b):            # mov edx, eax; shl eax, 16; sar edx, 31; idiv (32-bit shift)
    return idiv(s32(a << 16), b)


def _div10(a, b):
    return idiv(s32(a << 10), b)


def _edge(T, s, e, vclip, nattr):
    """0x414c94 / 0x414bc0 / 0x4142e4 / 0x414210: rows, x (16.16) and step, attributes (<< 10) and steps"""
    if not vclip:
        dy = e[1] - s[1]
        if dy <= 0:
            return None
        x = s32(s[0] << 16)
        A = [s32(s[2 + i] << 10) for i in range(nattr)]
        if dy > 2:
            dx = _div16(e[0] - s[0], dy)
            dA = [_div10(e[2 + i] - s[2 + i], dy) for i in range(nattr)]
        elif dy == 2:
            dx = s32((e[0] << 16) - (s[0] << 16)) >> 1
            dA = [s32((e[2 + i] << 10) - (s[2 + i] << 10)) >> 1 for i in range(nattr)]
        else:
            dx, dA = 0, [0] * nattr             # not used
        return [dy, x, dx, A, dA]
    full = e[1] - s[1]
    n = full
    if e[1] - T.ymax > 0:
        n -= e[1] - T.ymax
    skip = T.ymin - s[1] if s[1] < T.ymin else 0
    n -= skip
    if n <= 0:
        return None
    dx = _div16(e[0] - s[0], full)
    x = s32(dx * skip + (s[0] << 16))
    dA = [_div10(e[2 + i] - s[2 + i], full) for i in range(nattr)]
    A = [s32(dA[i] * skip + (s[2 + i] << 10)) for i in range(nattr)]
    return [n, x, dx, A, dA]


def _setup(T, verts, nattr):
    d, s, e = _sort3(*verts)
    if e[1] <= T.ymin or d[1] >= T.ymax:
        return None
    vclip = (1 if e[1] > T.ymax else 0) + (1 if d[1] < T.ymin else 0)
    row0 = max(d[1], T.ymin)
    xclip = any(v[0] > T.xmax for v in (d, s, e)) or any(v[0] < T.xmin for v in (d, s, e))
    dyt = e[1] - d[1]
    if dyt <= 0:
        return None
    t = _div16(s[1] - d[1], dyt)
    w = s32(t * (e[0] - d[0]) + ((d[0] - s[0]) << 16))
    if w == 0:
        return None
    if abs(w) <= 0x1000:
        w = s32(w << 12)
    steps = []
    for i in range(nattr):
        num = s32(t * (e[2 + i] - d[2 + i]) + ((d[2 + i] - s[2 + i]) << 16))
        steps.append(idiv(num * 1024, w))
    return d, s, e, vclip, row0, xclip, w, steps


def _walk(T, d, s, e, vclip, w, nattr, span):
    """the edge lists of 0x414f13 / 0x41448a: a long edge and two short ones; span(y, xl, xr, A) per row"""
    if w < 0:
        left = [(d, e)]
        right = [(d, s), (s, e)]
    else:
        right = [(d, e)]
        left = [(d, s), (s, e)]
    L = None
    while left:
        L = _edge(T, *left.pop(0), vclip, nattr)
        if L is not None:
            break
        if not left:
            return
    if L is None:
        return
    R = None
    while right:
        a, b = right.pop(0)
        R = _edge(T, a, b, vclip, 0)
        if R is not None:
            break
        if not right:
            return
    if R is None:
        return
    y = max(d[1], T.ymin)
    while True:
        span(y, L[1], R[1], L[3])
        y += 1
        L[0] -= 1
        if L[0] > 0:
            L[1] = s32(L[1] + L[2])
            L[3] = [s32(a + b) for a, b in zip(L[3], L[4])]
        else:
            if not left:
                return
            L = _edge(T, *left.pop(0), vclip, nattr)
            if L is None:
                return
        R[0] -= 1
        if R[0] > 0:
            R[1] = s32(R[1] + R[2])
        else:
            if not right:
                return
            a, b = right.pop(0)
            R = _edge(T, a, b, vclip, 0)
            if R is None:
                return


def bump_tri(T, env, bump, verts):
    """0x414f13: verts = 3 x (sx, sy, bu, bv, eu, ev); env = 256*256 uint8 (flat), bump = 256*256*2 uint8 (flat).
    Pixel: (dx, dy) = bump[bv][bu]; index = env[(ev + dy) & 255][(eu + dx) & 255], all coordinates 8.8 in the span."""
    r = _setup(T, verts, 4)
    if r is None:
        return
    d, s, e, vclip, row0, xclip, w, steps = r
    sbu, sbv, seu, sev = steps

    def b8(v):
        return ((v >> 8) & 0xff, (v >> 16) & 0xff)       # (fraction byte, integer byte) of a 16.16 value
    st = [b8(v) for v in steps]
    buf = T.buf

    def span(y, xl, xr, A):
        xl16, xr16 = xl >> 16, xr >> 16
        if xclip:
            n = xr16 - xl16
            if xr16 - T.xmax > 0:
                n -= xr16 - T.xmax
            skip = 0
            if xl16 < T.xmin:
                skip = T.xmin - xl16
                xl16 = T.xmin
            n -= skip
            if n <= 0:
                return
            vals = [s32(steps[i] * skip + A[i]) for i in range(4)]
        else:
            n = xr16 - xl16
            if n <= 0:
                return
            vals = A
        bu = ((vals[0] >> 8) & 0xffff)             # 8.8: integer byte, fraction byte
        bv = ((vals[1] >> 8) & 0xffff)
        eu = ((vals[2] >> 8) & 0xffff)
        ev = ((vals[3] >> 8) & 0xffff)
        dbu = st[0][1] << 8 | st[0][0]
        dbv = st[1][1] << 8 | st[1][0]
        deu = st[2][1] << 8 | st[2][0]
        dev = st[3][1] << 8 | st[3][0]
        o = y * T.pitch + xl16
        for i in range(n):
            bi = ((bv >> 8) << 8 | (bu >> 8)) * 2
            dx, dy = bump[bi], bump[bi + 1]
            u = ((eu >> 8) + dx) & 0xff
            v = ((ev >> 8) + dy) & 0xff
            buf[o + i] = env[v << 8 | u]
            bu = (bu + dbu) & 0xffff
            bv = (bv + dbv) & 0xffff
            eu = (eu + deu) & 0xffff
            ev = (ev + dev) & 0xffff
    _walk(T, d, s, e, vclip, w, 4, span)


def tex_tri(T, tex, verts):
    """0x41448a: verts = 3 x (sx, sy, u, v); affine, spans from round(xl) to round(xr), texture coordinates
    pre-stepped by the left edge's fraction in eighths and sampled at the pixel centre (+ step / 2)."""
    r = _setup(T, verts, 2)
    if r is None:
        return
    d, s, e, vclip, row0, xclip, w, steps = r
    du, dv = steps
    tu = [0, du >> 3, du >> 2, (du >> 2) + (du >> 3)]
    tv = [0, dv >> 3, dv >> 2, (dv >> 2) + (dv >> 3)]
    buf = T.buf

    def span(y, xl, xr, A):
        right = (xr >> 16) + ((xr >> 15) & 1)
        c = (xl >> 15) & 1
        left = (xl >> 16) + c
        u, v = A
        if xclip:
            n = right - left
            if right - T.xmax > 0:
                n -= right - T.xmax
            skip = 0
            if left < T.xmin:
                skip = T.xmin - left
                left = T.xmin
            n -= skip
            if n <= 0:
                return
            u = s32(du * skip + u)
            v = s32(dv * skip + v)
        else:
            n = right - left
            if n <= 0:
                return
        if c == 0:
            k = (xl & 0x7fff) >> 13
            u = s32(u - tu[k])
            v = s32(v - tv[k])
        else:
            k = (0xffff - (xl & 0xffff)) >> 13
            u = s32(u + tu[k])
            v = s32(v + tv[k])
        u = s32(u + (du >> 1))
        v = s32(v + (dv >> 1))
        # 8.16 in the loop: integer byte (bits 16..23) and the 16-bit fraction
        uu = u & 0xffffff
        vv = v & 0xffffff
        duu = du & 0xffffff
        dvv = dv & 0xffffff
        o = y * T.pitch + left
        for i in range(n):
            buf[o + i] = tex[((vv >> 16) & 0xff) << 8 | ((uu >> 16) & 0xff)]
            uu = (uu + duu) & 0xffffff
            vv = (vv + dvv) & 0xffffff
    _walk(T, d, s, e, vclip, w, 2, span)


# ---------------------------------------------------------------------------------------------------------------
# a whole frame

def objects(D, counts):
    """counts[k] = refreshes since the start for object k (the loop reads the clock once per object)"""
    out = []
    for k in range(4):
        t = obj_t(D, k, counts[k])
        o = transform(D, k, place(D, k, t))
        o.t = t
        out.append(o)
    return out


def render(D, counts, env_state=None, back=True):
    """one drawn frame: returns (320 x 200 frame, the transparent object's env map after the frame).
    env_state: the transparent env map left by the previous frame (letters stay in it until a letter is drawn again)."""
    obs = objects(D, counts)
    envL = bytes(D['env'][0].ravel())
    envR = (bytearray(D['env'][1].ravel()) if env_state is None else bytearray(env_state))
    bump = bytes(D['bump'].ravel())
    sph = obs[0]
    zs = sph.pv[2]
    nbehind = 0
    for o in obs[1:]:
        x, y, z = o.pv[:3]
        if z > zs + 500:                 # 0x406735: sphere z + 500 < letter z
            nbehind += 1
            if nbehind == 1:
                envR = bytearray(D['env'][1].ravel())
            project(o, 0x10000, 0x10000, -(z - zs + 10000), x, y, 0, 128, 128)
            backface(o)
            T = Target(envR, 256, 0, 0, 256, 256)
            for fi in visible_sorted(o):
                f = o.faces[fi]
                vs = [(o.S[f[j]][0], o.S[f[j]][1], o.N[f[3 + j]][0], o.N[f[3 + j]][1]) for j in range(3)]
                tex_tri(T, envL, vs)
    for o in obs:
        x, y, z = o.pv[:3]
        project(o, 0xd8f0, 0xb4c6, -7000, x, y, z, 160, 100)
        backface(o)
    order = sorted(range(4), key=lambda k: -obs[k].pv[2])      # 0x415480: stable, farthest first
    fb = bytearray(D['back'].ravel()) if back else bytearray(64000)
    T = Target(fb, 320, 0, 0, 320, 200)
    for k in order:
        o = obs[k]
        env = envR if k == 0 else envL
        for fi in visible_sorted(o):
            f = o.faces[fi]
            vs = [(o.S[f[j]][0], o.S[f[j]][1], o.uv[f[j]][0], o.uv[f[j]][1], o.N[f[3 + j]][0], o.N[f[3 + j]][1])
                  for j in range(3)]
            bump_tri(T, env, bump, vs)
    return np.frombuffer(bytes(fb), np.uint8).reshape(200, 320), np.frombuffer(bytes(envR), np.uint8), obs
