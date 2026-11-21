"""Render any part's scene from a hooks_scene.py dump with the engine model and compare with the original's buffer.

What is taken from the dump (what the parts set): every object's own position, rotation, parent, flags, bias,
light parameters, vertex positions / u, v / normals / shades, face vertices / materials / rasterizers / flags, the
materials and the scene parameters. What the model computes: world transforms (hierarchy), object -> camera,
bounding-box culling, back faces, projection, face lists and keys, the 0x1000 texture stage, lighting, sorting,
near clipping, rasterizing (0x412db0, 0x414e5c). Textures and tables are read from INSIDE.DAT through the
pointers (the data block's base [0x424894] is read from a snapshot).

usage: python3 scene_model.py out/flower_dump.pkl ../../snaps/f4963.snap [--png out.png]
"""
import argparse
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile  # noqa: E402
import engine as E  # noqa: E402

L = E.L


def i32(b, o): return struct.unpack_from('<i', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def i16(b, o): return struct.unpack_from('<h', b, o)[0]
def mat(b, o): return [[L(struct.unpack_from('<d', b, o + 8 * (3 * r + c))[0]) for c in range(3)] for r in range(3)]


class Obj:
    pass


def parse(entry):
    o = Obj()
    h = entry['hdr']
    o.addr, o.pipe, o.parent = entry['addr'], u32(h, 0), u32(h, 0xc)
    o.pos = [i32(h, 0x10 + 4 * k) for k in range(3)]
    o.rot = mat(h, 0x28)
    o.flags = u32(h, 0xc4)
    o.c8, o.cc, o.radius = i32(h, 0xc8), i32(h, 0xcc), i32(h, 0xf8)
    o.hdr = h
    o.bbox = [i32(h, 0xe0 + 4 * k) for k in range(6)]
    vidx = {}
    o.vs = []
    for k, (a, vb) in enumerate(entry['verts']):
        p = E.Vertex()
        p.idx = k
        p.x, p.y, p.z = i32(vb, 8), i32(vb, 0xc), i32(vb, 0x10)
        p.u, p.v, p.eu, p.ev = i16(vb, 0x28), i16(vb, 0x2a), i16(vb, 0x2c), i16(vb, 0x2e)
        p.shade, p.shade2 = i16(vb, 0x30), i16(vb, 0x32)
        p.n = (i32(vb, 0x34), i32(vb, 0x38), i32(vb, 0x3c))
        p.flags = 0
        o.vs.append(p)
        vidx[a] = k
    o.faces = []
    for a, fb in entry['faces']:
        o.faces.append({'v': tuple(vidx[u32(fb, 0x14 + 4 * j)] for j in range(3)),
                        'n': (i32(fb, 0x20), i32(fb, 0x24), i32(fb, 0x28)), 'mat': u32(fb, 0x10),
                        'draw': u32(fb, 8), 'flags': u32(fb, 0x30)})
    return o


def chain(o, objs):
    """0x40c224: (R, P) from the parents' own rotations and positions"""
    if not o.parent:
        return o.rot, [L(x) for x in o.pos]
    R, P = chain(objs[o.parent], objs)
    t = E.mat_vec(R, [L(x) for x in o.pos])
    P = [E.D(P[k] + t[k]) for k in range(3)]
    return E.mat_mul(R, o.rot), P


def world(o, objs):
    """0x40c2b8"""
    if not o.parent or (o.flags & 8 and o.flags & 0x10):
        o.wrot, o.wpos = o.rot, list(o.pos)
        return
    R, P = chain(o, objs)
    o.wpos = list(o.pos) if o.flags & 0x10 else [E.trunc(x) for x in P]
    o.wrot = o.rot if o.flags & 8 else R


def bbox_cull(o, M, T, sc):
    """0x41a5b8: True when all 8 corners share a clip flag"""
    if o.flags & 0x2000:
        return False
    b = o.bbox
    a_and, a_or = -1, 0
    for cx in (b[0], b[3]):
        for cy in (b[1], b[4]):
            for cz in (b[2], b[5]):
                v = [L(cx) * 128, L(cy) * 128, L(cz) * 128]
                v = E.mat_vec(M, v)
                p = [E.D(L(T[k]) + v[k]) for k in range(3)]
                fl = 0
                if not p[2] > 16384:
                    fl = 0x100000
                else:
                    d = E.D(L(1) / (p[2] * L(128)))
                    x = E.D(L(sc.xs) * d * p[0] + L(sc.cx))
                    y = E.D(L(sc.ys) * d * p[1] + L(sc.cy))
                    if L(sc.x0) > x: fl |= 0x10000
                    if not L(sc.x1) > x: fl |= 0x20000
                    if L(sc.y0) > y: fl |= 0x40000
                    if not L(sc.y1) > y: fl |= 0x80000
                a_and &= fl
                a_or |= fl
    return a_or != 0 and a_and != 0


def stage_1000(o, used, M):
    """0x41acc4"""
    for p in o.vs:
        if p.idx not in used:
            continue
        n = [L(2 * p.n[0]), L(2 * p.n[1]), L(2 * p.n[2])]
        U = E.D((n[1] * M[0][1] + n[0] * M[0][0]) + n[2] * M[0][2])
        V = E.D((n[0] * M[1][0] + n[1] * M[1][1]) + n[2] * M[1][2])
        iu = E.trunc(U + L(2048))
        iv = E.trunc(V + L(2048))
        p.u = E.s16(-(iu & 0xfff) - E.tdiv(p.px, 64))
        p.v = E.s16(E.tdiv(p.py, 64) + (iv & 0xfff))


def render(d, dat, base, check=None):
    sb = d['scene']
    sc = E.Scene(width=i32(sb, 0x20), height=i32(sb, 0x24), x0=i32(sb, 0x28), x1=i32(sb, 0x2c), y0=i32(sb, 0x30),
                 y1=i32(sb, 0x34), fov=i32(sb, 0x3c), near=i32(sb, 0x44), ambient=E.s16(i32(sb, 0x40)),
                 flags=i32(sb, 0x4c) & 0xff, cx=i32(sb, 0x10), cy=i32(sb, 0x14), far=i32(sb, 0x48))
    objs = {}
    order = []
    for ent in d['objs']:
        o = parse(ent)
        objs[o.addr] = o
        order.append(o)
    for o in order:
        world(o, objs)
    cam = objs[d['cam']]
    lights = [o for o in order if o.flags & 0x80 and not o.flags & 1]
    lst = []
    for o in order:
        if o.pipe == 0x42381c and not o.flags & 1:
            F = billboard_item(o, cam, sc)
            if F is not None:
                lst = [F] + lst
            continue
        if o.pipe != 0x423a84 or o.flags & 1:
            continue
        M, T = E.object_to_camera(cam.wrot, cam.wpos, o.wrot, o.wpos)
        if bbox_cull(o, M, T, sc):
            continue
        faces = [f['v'] for f in o.faces]
        if o.flags & 0x100:
            vis = [not f['flags'] & 1 for f in o.faces]
        else:
            vis = E.backface(o.vs, faces, [f['n'] for f in o.faces], o.wrot, o.wpos, cam.wpos,
                             double_sided={k for k, f in enumerate(o.faces) if f['flags'] & 0x100})
            vis = [v and not (f['flags'] & 1 and not f['flags'] & 0x100) for v, f in zip(vis, o.faces)]
        used = {k for f, v in zip(faces, vis) if v for k in f}
        E.project(o.vs, used, M, T, sc)
        fl = E.face_list(o.vs, faces, vis, bias=o.cc)
        used2 = {p.idx for F in fl for p in F.v}
        if o.flags & 0x1000:
            stage_1000(o, used2, M)
        if o.flags & 0x40:
            E.light_vertices(o.vs, used2, o.wrot, o.wpos, o.pos, o.radius,
                             [(L_.wpos, L_.pos, L_.c8, L_.cc) for L_ in lights], sc.ambient)
        for F in fl:
            F.fn = (o, o.faces[F.idx])
        lst = fl + lst                  # each object's faces are put in front of the list in its own order
    if check:
        check(objs, order)
    buf = np.frombuffer(d['buf0'], np.uint8).reshape(200, 320).copy() if 'buf0' in d else \
        np.zeros((200, 320), np.uint8)
    dd = np.frombuffer(dat.data, np.uint8)
    for F in E.sort_faces(lst):
        o, f = F.fn
        if f is None:
            draw_billboard(buf, o, sc, dd, base, d.get('mem'))
            continue
        m = d['mats'][f['mat']]
        verts = F.v
        if i32(m, 0x18) & 1:            # 0x41be3c: the material's fixed u, v go into the face's vertices
            for j, p in enumerate(verts):
                p.u, p.v = E.s16(i32(m, 0x20 + 8 * j)), E.s16(i32(m, 0x24 + 8 * j))
        draw = rasterizer(f['draw'], buf, sc, m, dd, base, d.get('mem'))
        E.clip_and_draw(verts, sc, draw)
    return buf


_CACHE = {}


def memory(a, dd, base, mem):
    """bytes at address a: from the dump's copies when present (generated textures), else from INSIDE.DAT
    (the data block is at [0x424894])"""
    key = (a, id(mem))
    if key not in _CACHE:
        if mem and a in mem:
            _CACHE[key] = np.frombuffer(mem[a], np.uint8).astype(np.int64)
        else:
            _CACHE[key] = dd[a - base:a - base + 0x40000].astype(np.int64)
    return _CACHE[key]


def rasterizer(kind, buf, sc, m, dd, base, mem=None):
    """the six rasterizers (face+8) as one routine: the same setup and gradients, the pixel by kind"""
    def ptr(o):
        a = u32(m, o)
        return memory(a, dd, base, mem) if a else None
    tex, tex2, t1, t2 = ptr(0), ptr(4), ptr(8), ptr(0xc)
    umask, vmask, wlog, hlog = i32(m, 0x38), i32(m, 0x3c), m[0x40], m[0x41]
    chans = {0x412db0: ('u', 'v'), 0x414e5c: ('u', 'v', 'shade'), 0x412264: ('u', 'v', 'shade', 'shade2'),
             0x41669c: ('u', 'v'), 0x416dc8: ('shade',), 0x4172e0: ('u', 'v', 'eu', 'ev')}[kind]

    def draw(v0, v1, v2):
        s = E.setup(v0, v1, v2, sc)
        if s is None:
            return
        g = {c: list(E.gradients(s, c)) for c in chans}
        xl, xu, xd = s['xl'], s['xu'], s['xd']
        y = s['row0']
        for left in range(s['rows'], 0, -1):
            short = xu if left > s['rows_lower'] else xd
            l, r = (short, xl) if s['W'] > 0 else (xl, short)
            ix0, ix1 = max(l >> 16, sc.x0), min(r >> 16, sc.x1)
            if ix1 > ix0:
                cs = np.arange(ix0, ix1, dtype=np.int64)
                A = {c: (((g[c][1] * cs + g[c][0]) & 0xffffffff) >> 16) & 0xff for c in chans}
                if 'u' in A:                # u, v are fractions of the texture: 4096 = its width / height
                    A['u'] = (((g['u'][1] * cs + g['u'][0]) & 0xffffffff) >> (24 - wlog)) & umask
                    A['v'] = (((g['v'][1] * cs + g['v'][0]) & 0xffffffff) >> (24 - hlog)) & vmask
                    texel = tex[A['u'] + (A['v'] << wlog)]
                dst = buf[y, ix0:ix1].astype(np.int64)
                if kind == 0x412db0:
                    px = texel
                elif kind == 0x414e5c:
                    px = t1[(A['shade'] << 8) + texel]
                elif kind == 0x412264:
                    px = t2[(A['shade2'] << 8) + t1[(A['shade'] << 8) + texel]]
                elif kind == 0x41669c:
                    px = t1[(texel << 8) + dst]
                elif kind == 0x416dc8:
                    px = t1[(A['shade'] << 8) + dst]
                else:
                    px = t1[(tex2[A['eu'] + (A['ev'] << 8)] << 8) + texel]
                buf[y, ix0:ix1] = px
            xl, xu, xd = E.s32(xl + s['sl']), E.s32(xu + s['su']), E.s32(xd + s['sd'])
            for c in chans:
                g[c][0] = E.s32(g[c][0] + g[c][2])
            y += 1
    return draw


def billboard_item(o, cam, sc):
    """0x4157d4: the centre in camera space, the key; None when behind"""
    M, T = E.object_to_camera(cam.wrot, cam.wpos, o.wrot, o.wpos)
    o.T = [np.float32(t) for t in T]
    if 0 > o.T[2]:
        return None
    k = E.trunc(L(i32(o.hdr, 0xf4)) + L(o.T[2]))
    if k < 0:
        k = 0
    F = E.Face()
    F.key, F.idx, F.v = E.tdiv(k, 16), 0, ()
    F.fn = (o, None)
    return F


def draw_billboard(buf, o, sc, dd, base, mem=None):
    """0x415878 (mode 0: 0x415dbc opaque, mode 1: 0x415dc8 through the table [0xdc])"""
    h = o.hdr
    f32 = lambda off: L(struct.unpack_from('<f', h, off)[0])
    mode = 0 if u32(h, 0xd0) == 0x415dbc else 1
    zf = E.D(L(o.T[2]) * L(0.0078125))
    if L(E.tdiv(sc.near, 128)) > zf or L(sc.far) < zf:
        return
    s, size = f32(0xec), f32(0xf0)
    w, hh, pitch = i32(h, 0xe4), i32(h, 0xe8), i32(h, 0xe0)
    xs1024 = L(sc.xs) * L(1.0 / 1024)
    Xn = (L(o.T[0]) * s) * xs1024
    iz = L(1) / zf
    Yn = (L(o.T[1]) * s) * xs1024
    X, Y = E.D(Xn * iz), E.D(Yn * iz)
    Wd, Hd = E.D((L(sc.xs) * (L(w) * size)) * iz), E.D((L(sc.ys) * (L(hh) * size)) * iz)
    if not Wd > 0 or not Hd > 0:
        return
    Ys = L(sc.cy * 16) + Y
    iH = E.trunc(Hd)
    y0 = E.trunc(Ys - Hd * L(0.5))
    y1 = y0 + iH
    tW = L(E.trunc(Wd))
    iW = E.trunc(Wd)
    du = E.D(L(16) * L(w) / tW)
    dv = E.D(L(16) * L(hh) / L(iH))
    Xs = L(sc.cx * 16) + X
    x0 = E.trunc(Xs - Wd * L(0.5))
    x1 = x0 + iW
    voff, uoff = L(0), L(0)
    if y0 < sc.y0 * 16:
        voff = E.D(L(0) - L(y0 - sc.y0 * 16) * dv * L(0.0625))
        y0 = sc.y0 * 16
    if x0 < sc.x0 * 16:
        uoff = E.D(uoff - L(x0 - sc.x0 * 16) * du * L(0.0625))
        x0 = sc.x0 * 16
    fy, fx = y0 & 15, x0 & 15
    ry0, ry1, rx0, rx1 = E.tdiv(y0, 16), E.tdiv(y1, 16), E.tdiv(x0, 16), E.tdiv(x1, 16)
    rx1, ry1 = min(rx1, sc.x1), min(ry1, sc.y1)
    if rx1 <= rx0 or ry0 >= ry1:
        return
    u0 = E.D(uoff + L(15 - fx) * du * L(0.0625))
    U16, dU16 = E.D(L(65536) * u0), E.D(du * L(65536))
    v = E.D(L(15 - fy) * dv * L(0.0625) + voff)
    src = memory(u32(h, 0xd8), dd, base, mem)
    tab = memory(u32(h, 0xdc), dd, base, mem) if mode else None
    for y in range(ry0, ry1):
        row = E.trunc(v) * pitch
        iu, idu = E.trunc(U16), E.trunc(dU16)
        for c in range(rx0, rx1):
            t = int(src[row + (iu >> 16)])
            buf[y, c] = t if mode == 0 else tab[t * 256 + int(buf[y, c])]
            iu += idu
        v = E.D(v + dv)


def draw_tex(buf, tex, sc, umask, vmask, wlog):
    """rasterizer 0x412db0: pixel = tex[u, v]"""
    def draw(v0, v1, v2):
        s = E.setup(v0, v1, v2, sc)
        if s is None:
            return
        ru, gxu, gyu = E.gradients(s, 'u')
        rv, gxv, gyv = E.gradients(s, 'v')
        xl, xu, xd = s['xl'], s['xu'], s['xd']
        y = s['row0']
        for left in range(s['rows'], 0, -1):
            short = xu if left > s['rows_lower'] else xd
            l, r = (short, xl) if s['W'] > 0 else (xl, short)
            ix0, ix1 = max(l >> 16, sc.x0), min(r >> 16, sc.x1)
            if ix1 > ix0:
                cs = np.arange(ix0, ix1, dtype=np.int64)
                U = (gxu * cs + ru) & 0xffffffff
                V = (gxv * cs + rv) & 0xffffffff
                buf[y, ix0:ix1] = tex[((U >> 16) & umask) + (((V >> 16) & vmask) << wlog)]
            xl, xu, xd = E.s32(xl + s['sl']), E.s32(xu + s['su']), E.s32(xd + s['sd'])
            ru, rv = E.s32(ru + gyu), E.s32(rv + gyv)
            y += 1
    return draw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump')
    ap.add_argument('snap')
    ap.add_argument('--png', default='')
    a = ap.parse_args()
    import unicorn  # noqa: F401  (unpickling the snapshot needs it)
    base = struct.unpack_from('<I', pickle.load(open(a.snap, 'rb'))['mem'], 0x424894)[0]
    dat = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
    D = pickle.load(open(a.dump, 'rb'))
    tot = bad = 0
    for i in range(len(D) - 1):
        if D[i]['when'] != 'before' or D[i + 1]['when'] != 'after':
            continue
        buf = render(D[i], dat, base)
        orig = np.frombuffer(D[i + 1]['buf'], np.uint8).reshape(200, 320)
        n = int((buf != orig).sum())
        tot += 1
        bad += n
        print('frame %d: pixels differing %d' % (D[i]['frame'], n))
        if a.png and n:
            from PIL import Image
            img = np.concatenate([orig, buf, np.where(buf != orig, 255, 0).astype(np.uint8)], axis=1)
            Image.fromarray(img).resize((1920, 400)).save(a.png)
            a.png = ''
    print('%d frames, %d pixels differ (%.4f%%)' % (tot, bad, 100.0 * bad / max(tot * 64000, 1)))


if __name__ == '__main__':
    main()
