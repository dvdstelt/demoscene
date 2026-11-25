"""Integer-exact model of one GenericEffect frame (0x409d74's loop body) for the shading types 9, 10 and 11.

usage: python3 render.py gen/gen.ini runs/land 0 5 12     model draws 0, 5, 12 of runs/land.gentxt and compare
                                                          them with the pages the emulator drew (runs/land.pages)
The .gentxt / .pages come from run_vivid.py --gen-dump (this directory's copy).
"""
import sys

import numpy as np

import gen
from gen import s16, s32, idiv

W, H = 320, 200


class Scene:
    def __init__(self, ininame):
        cfg = gen.read_ini(ininame)
        self.cfg = cfg
        n = int(cfg['NumberOfObjects'])
        self.looping = int(cfg.get('UseSplineLooping', '0'))
        self.objs = []
        cache = {}
        for i in range(n):
            name = cfg['ObjectName%d' % i]
            get = lambda key, d=None: cfg.get(name + key, cfg.get('Common' + key, d))
            o = type('Obj', (), {})()
            o.name = name
            o.type = int(get('ShadingType'))
            o.m3d = cache.setdefault(get('Object'), gen.read_m3d(get('Object')))
            spl = get('SplinePath')
            cols = gen.read_spl(spl)
            o.recs = [gen.spline_prep(c) for c in cols[:6]]
            o.n = len(cols[0])
            o.max = (o.n - 1) << 16
            sp = int(get('SplineSpeed'))
            o.speed = idiv(0x10000, sp) if sp else 0
            o.start = int(get('SplineStartPos'))
            if o.type in (9, 10, 11):
                env, _ = gen.pcx(get('EnvironmentMap')) if o.type != 11 else (None, None)
                o.env = env
                o.bump = gen.bump_table(get('BumpMap'), 2)
            if o.type == 11:
                o.tex, _ = gen.pcx(get('TextureMap'))
                o.lit = np.frombuffer(gen.load(get('TextureLite'))[:65536].ljust(65536, b'\0'), np.uint8).reshape(256, 256)
                o.phong = gen.phong_map(int(get('PhongShades')), int(get('PhongAmb')), int(get('PhongDiff')),
                                        int(get('PhongSpec')), int(get('PhongExp')))
            self.objs.append(o)
        self.bgpic = None
        if int(cfg.get('UseBackgroundPic', '0')):
            self.bgpic, _ = gen.pcx(cfg['BackgroundPic'])
        self.bgcol = int(cfg.get('BackgroundColor', '0'))
        self.envtab = {i: gen.env_formula(i) for i in range(-256, 257)}

    @staticmethod
    def t_from_pos(o, pos):
        """the t that gives this (wrapped) spline position: pos = (start + speed * t) mod max"""
        return idiv(pos - o.start if pos >= o.start else pos + o.max - o.start, o.speed) if o.speed else 0

    def state(self, o, t):
        """GenericEffect 0x409e4d: position for t = counter - t0 refreshes (no looping reset modelled)"""
        pos = o.start + o.speed * t
        pos = pos - o.max * int(pos / o.max)             # idiv remainder (dividend >= 0 here)
        vals = [gen.spline_eval(r, pos) >> 8 for r in o.recs]
        return pos, vals


def transform(o, vals):
    """reset, rotY(b), rotX(a), rotZ(c) on vertices and normals; normals -> envmap coords (0x414160)"""
    X, Y, Z, A, B, C = vals
    sa, sb, sc = gen.sin16(A), gen.sin16(B), gen.sin16(C)
    ca, cb, cc = gen.cos16(A), gen.cos16(B), gen.cos16(C)

    def rot(p, q, c, s):            # p' = (p c - q s) >> 16, q' = (p s + q c) >> 16, as 16-bit words
        return np.vectorize(s16)((p * c - q * s) >> 16), np.vectorize(s16)((p * s + q * c) >> 16)

    def apply(v):
        x, y, z = v[:, 0].copy(), v[:, 1].copy(), v[:, 2].copy()
        x, z = rot(x, z, cb, sb)
        y, z = rot(y, z, ca, sa)
        y, x = rot(y, x, cc, sc)
        return x, y, z

    vx, vy, vz = apply(o.m3d.xyz)
    nx, ny, nz = apply(o.m3d.normals)
    return (vx, vy, vz), (nx, ny, nz)


def project(v, X, Y, Z):
    vx, vy, vz = v
    kx = (0xd8f0 * -7000) >> 20                   # -371
    ky = -((0xb4c6 * -7000) >> 20)                # 309
    n = len(vx)
    sx = np.zeros(n, np.int64)
    sy = np.zeros(n, np.int64)
    for i in range(n):
        den = -7000 - (int(vz[i]) + Z)
        if den == 0:
            continue                               # keeps the old screen coordinates (not modelled)
        sy[i] = s16(idiv((int(vy[i]) + Y) * ky, den) + 100)
        sx[i] = s16(idiv((int(vx[i]) + X) * kx, den) + 160)
    return sx, sy


def hpsort_desc(a, key):
    """0x411a30 (Numerical Recipes hpsort with the comparisons reversed: descending)"""
    n = len(a)
    if n < 2:
        return a
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
            if j < ir and key(ra[j]) > key(ra[j + 1]):
                j += 1
            if key(rra) > key(ra[j]):
                ra[i] = ra[j]
                i = j
                j <<= 1
            else:
                j = ir + 1
        ra[i] = rra
    return ra[1:]


def isort_desc(a, key):
    """0x4119f0 / 0x415480: insertion sort, descending, stable"""
    a = list(a)
    for i in range(1, len(a)):
        x = a[i]
        j = i - 1
        while j >= 0 and key(a[j]) < key(x):
            a[j + 1] = a[j]
            j -= 1
        a[j + 1] = x
    return a


def b16(v):
    return v & 0xffff


def raster(page, verts, pix):
    """0x414f13 / 0x418313. verts: 3 tuples (sx, sy, a, b, c, d) in the drawer's order;
    pix(a16, b16, c16, d16) -> index, the coordinates as 8.8 (int byte << 8 | frac byte)."""
    e, s, d = list(verts[0]), list(verts[1]), list(verts[2])   # edx, esi, edi
    if e[1] > s[1]: e, s = s, e
    if e[1] > d[1]: e, d = d, e
    if s[1] > d[1]: s, d = d, s
    top, mid, bot = e, s, d
    if bot[1] <= 0 or top[1] >= H:
        return
    yclip = (bot[1] > H) + (top[1] < 0)
    row = max(top[1], 0)
    xclip = any(v[0] > W for v in (top, mid, bot)) or any(v[0] < 0 for v in (top, mid, bot))
    dy = bot[1] - top[1]
    if dy <= 0:
        return
    t = s32(idiv(s32((mid[1] - top[1]) << 16), dy))
    width = s32(s32(t * (bot[0] - top[0])) + s32((top[0] - mid[0]) << 16))
    if width == 0:
        return
    if abs(width) <= 0x1000:
        width = s32(width << 12)
    grads = []
    for k in (2, 3, 4, 5):
        num = s32(s32(t * (bot[k] - top[k])) + s32((top[k] - mid[k]) << 16))
        grads.append(s32(idiv(num << 10, width)))
    g8 = [b16(g >> 8) for g in grads]            # the self-modified 8.8 immediates

    def edge(p, q, attrs):
        cnt = q[1] - p[1]
        if not yclip:
            if cnt <= 0:
                return None
            x = s32(p[0] << 16)
            if cnt > 2:
                dx = s32(idiv(s32((q[0] - p[0]) << 16), cnt))
            elif cnt == 2:
                dx = s32((s32(q[0] << 16) - x) >> 1)
            else:
                dx = 0
            A, dA = [], []
            for k in (2, 3, 4, 5):
                a0 = s32(p[k] << 10)
                A.append(a0)
                if cnt > 2:
                    dA.append(s32(idiv((q[k] - p[k]) << 10, cnt)))
                elif cnt == 2:
                    dA.append(s32((s32(q[k] << 10) - a0) >> 1))
                else:
                    dA.append(0)
            return [cnt, x, dx, A, dA]
        full = cnt
        n = cnt
        if q[1] - H > 0:
            n -= q[1] - H
        skip = -p[1] if p[1] < 0 else 0
        n -= skip
        if n <= 0:
            return None
        dx = s32(idiv(s32((q[0] - p[0]) << 16), full))
        x = s32(s32(dx * skip) + s32(p[0] << 16))
        A, dA = [], []
        for k in (2, 3, 4, 5):
            g = s32(idiv((q[k] - p[k]) << 10, full))
            dA.append(g)
            A.append(s32(s32(g * skip) + s32(p[k] << 10)))
        return [n, x, dx, A, dA]

    if width < 0:
        left_chain = [(top, bot)]
        right_chain = [(top, mid), (mid, bot)]
    else:
        left_chain = [(top, mid), (mid, bot)]
        right_chain = [(top, bot)]

    def next_edge(chain):
        while chain:
            p, q = chain.pop(0)
            r = edge(p, q, True)
            if r is not None:
                return r
            return None                 # the code stops when a segment has no rows, except the first one
        return None

    # first segments: the code tries (top, mid) and, if it has no rows, (mid, bot); the long edge has one try
    def first(chain):
        r = edge(*chain[0], True)
        if r is None and len(chain) > 1:
            chain.pop(0)
            r = edge(*chain[0], True)
        chain.pop(0)
        return r

    if width < 0:
        L = first(left_chain)
        if L is None:
            return
        R = first(right_chain)
        if R is None:
            return
    else:
        R = first(right_chain)
        if R is None:
            return
        L = first(left_chain)
        if L is None:
            return
    y = row
    while True:
        xl, xr = L[1] >> 16, R[1] >> 16
        A = L[3]
        if xclip:
            n = xr - xl
            if xr - W > 0:
                n -= xr - W
            skip = 0
            if xl < 0:
                skip = -xl
                xl = 0
            n -= skip
            cur = [b16(s32(s32(grads[k] * skip) + A[k]) >> 8) for k in range(4)]
        else:
            n = xr - xl
            cur = [b16(A[k] >> 8) for k in range(4)]
        if n > 0 and 0 <= y < H:
            c0, c1, c2, c3 = cur
            for x in range(xl, xl + n):
                page[y, x] = pix(c0, c1, c2, c3)
                c0 = b16(c0 + g8[0]); c1 = b16(c1 + g8[1]); c2 = b16(c2 + g8[2]); c3 = b16(c3 + g8[3])
        y += 1
        L[0] -= 1
        if L[0] > 0:
            L[1] = s32(L[1] + L[2])
            L[3] = [s32(L[3][k] + L[4][k]) for k in range(4)]
        else:
            if not left_chain:
                return
            L = edge(*left_chain.pop(0), True)
            if L is None:
                return
        R[0] -= 1
        if R[0] > 0:
            R[1] = s32(R[1] + R[2])
        else:
            if not right_chain:
                return
            R = edge(*right_chain.pop(0), True)
            if R is None:
                return


def pixel_fn(o):
    """the inner loops; a, b, c, d: the vertex values at offsets 8, 0xc, 0x10, 0x14 as 8.8"""
    if o.type in (9, 10):
        env, bump = o.env, o.bump

        def pix(a, b, c, d):
            u, v = a >> 8, b >> 8
            dx, dy = bump[v, u]
            return env[((d >> 8) + dy) & 255, ((c >> 8) + dx) & 255]
        return pix
    tex, bump, lit, ph = o.tex, o.bump, o.lit, o.phong

    def pix(a, b, c, d):
        u, v = a >> 8, b >> 8
        dx, dy = bump[v, u]
        shade = ph[((d >> 8) + dy) & 255, ((c >> 8) + dx) & 255]
        return lit[shade, tex[v, u]]
    return pix


def render_frame(sc, ts, page=None):
    """ts: per object the frame counter minus t0. Returns the 200x320 page."""
    if page is None:
        page = (sc.bgpic[:H, :W].copy().astype(np.int64) if sc.bgpic is not None
                else np.full((H, W), sc.bgcol, np.int64))
    per = []
    for i, o in enumerate(sc.objs):
        pos, vals = sc.state(o, ts[i])
        v, nrm = transform(o, vals)
        sx, sy = project(v, vals[0], vals[1], vals[2])
        ex = np.array([sc.envtab[int(n) >> 6] for n in nrm[0]])
        ey = np.array([sc.envtab[int(n) >> 6] for n in nrm[1]])
        faces = o.m3d.groups[0][1]
        flags = []
        for f in faces:
            a, b, c = f[0], f[1], f[2]
            p1 = s16(sy[a] - sy[b]) * s16(sx[c] - sx[b])
            p2 = s16(sx[a] - sx[b]) * s16(sy[c] - sy[b])
            flags.append(1 if p2 < p1 else 0)
        per.append((o, vals, v, sx, sy, ex, ey, flags))
    order = isort_desc(list(range(len(sc.objs))), key=lambda i: per[i][1][2])
    for i in order:
        o, vals, v, sx, sy, ex, ey, flags = per[i]
        Z = vals[2]
        faces = o.m3d.groups[0][1]
        vz = v[2]
        keys = []
        for fi, f in enumerate(faces):
            if not flags[fi] and all(int(vz[f[k]]) + Z > -6720 for k in range(3)):
                zs = sorted([int(vz[f[0]]), int(vz[f[1]]), int(vz[f[2]])])
                keys.append((fi, s16(zs[0] + (s16(zs[2] - zs[0]) >> 1))))
        if len(keys) >= 2:
            keys = isort_desc(keys, key=lambda k: k[1]) if len(keys) < 200 else hpsort_desc(keys, key=lambda k: k[1])
        pix = pixel_fn(o)
        for fi, _ in keys:
            f = faces[fi]
            vs = []
            for k in range(3):
                vi, ni = f[k], f[3 + k]
                u, w = (o.m3d.uv[vi] >> 2) if o.m3d.uv is not None else (0, 0)
                if o.type == 10:
                    vs.append((int(sx[vi]), int(sy[vi]), int(ex[ni]), int(ey[ni]), int(ey[ni]) - 0x4000,
                               int(ex[ni]) - 0x4000))
                else:
                    vs.append((int(sx[vi]), int(sy[vi]), int(u), int(w), int(ex[ni]), int(ey[ni])))
            raster(page, vs, pix)
    return page


def read_gentxt(path):
    draws = []
    for line in open(path):
        f = line.split()
        if f[0] != 'draw':
            continue
        d = {'n': int(f[1]), 'refresh': int(f[3]), 'counter': int(f[7]), 'objs': []}
        for s in f[10:]:
            i, rest = s.split(':')
            d['objs'].append([int(x) for x in rest.split(',')])
        draws.append(d)
    return draws


def main():
    ini, prefix = sys.argv[1], sys.argv[2]
    sel = [int(x) for x in sys.argv[3:]]
    sc = Scene(ini)
    draws = read_gentxt(prefix + '.gentxt')
    pages = np.memmap(prefix + '.pages', np.uint8, 'r')
    for k in sel:
        d = draws[k]
        ts = []
        for i, o in enumerate(sc.objs):
            pos = d['objs'][i][0]
            t = sc.t_from_pos(o, pos)
            ts.append(t)
            pm, vals = sc.state(o, t)
            if pm != pos or vals != d['objs'][i][1:]:
                print('draw', k, 'object', i, 'state model', pm, vals, 'emulator', d['objs'][i])
        page = render_frame(sc, ts)
        ref = np.asarray(pages[k * 64000:(k + 1) * 64000]).reshape(H, W)
        diff = (page != ref)
        print('draw %d refresh %d t %s: %d pixels differ' % (k, d['refresh'], ts[:3], diff.sum()))
        if diff.any():
            ys, xs = np.nonzero(diff)
            print('  first', list(zip(ys[:8].tolist(), xs[:8].tolist(), page[ys[:8], xs[:8]].tolist(),
                                    ref[ys[:8], xs[:8]].tolist())))
            from PIL import Image
            img = np.concatenate([page.astype(np.uint8), ref, (diff * 255).astype(np.uint8)], 1)
            Image.fromarray(img).save(prefix + '_diff%d.png' % k)


if __name__ == '__main__':
    main()
