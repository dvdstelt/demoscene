import numpy as np, math, sys
from emu import Emu
from model import Fix, s32, trunc_div
tick = int(sys.argv[1])
e = Emu(tick)
sin = e.ii(0x22479, 1024)
objs = [e.d(0x6816)] if tick < 8000 else [e.d(e.d(0x33c0) + 32 * n) for n in range(32)]
V = e.ii(0x1e468, 9); cam = e.ii(0x1ea9d, 3); Lpt = e.ii(0x1e48c, 3)
shift = e.rd(0x42ac, 1)[0]
print('bucket shift byte', shift)
tot = 0; bad = 0
for ob in objs:
    nv, nf = e.i(ob + 0x6c), e.i(ob + 0x70); vl, fl = e.d(ob + 0x78), e.d(ob + 0x74)
    m = e.ii(ob, 9); pos = e.ii(ob + 0x48, 3)
    verts = [e.ii(vl + 0x24 * k, 3) for k in range(nv)]
    faces = []
    for k in range(nf):
        fa = fl + 0x4c * k
        vi = [(e.d(fa + 4 * q) - vl) // 0x24 for q in range(3)]
        nb = [(e.d(fa + 0xc + 4 * q) - fl) // 0x4c for q in range(3)]
        faces.append(dict(addr=fa, v=vi, nb=nb, flags0=e.rd(fa + 0x1d, 1)[0], base=e.rd(fa + 0x1e, 1)[0], n=e.ii(fa + 0x24, 3)))
    # normals model (0x3dc0)
    for f in faces:
        a, b, c = (verts[q] for q in f['v'])
        e1 = [b[q] - a[q] for q in range(3)]; e2 = [b[q] - c[q] for q in range(3)]
        n = [e1[1] * e2[2] - e2[1] * e1[2], e1[2] * e2[0] - e2[2] * e1[0], e1[0] * e2[1] - e2[0] * e1[1]]
        ln = int(np.rint(math.sqrt(sum(x * x for x in n)))) or 1
        nn = [trunc_div(x << 8, ln) for x in n]
        assert nn == f['n'], (nn, f['n'])
    e.call(0x3fb3, ebp=ob)
    W = Fix.mul(V, m)
    d = [pos[q] - cam[q] for q in range(3)]
    T = [s32(V[3 * r] * d[0] + V[3 * r + 1] * d[1] + V[3 * r + 2] * d[2]) for r in range(3)]
    L = [Lpt[q] - pos[q] for q in range(3)]
    Lo = [s32(m[c] * L[0] + m[3 + c] * L[1] + m[6 + c] * L[2]) >> 16 for c in range(3)]
    ln = math.sqrt(sum(x * x for x in Lo)) / 200.0
    Lo = [int(np.rint(x / ln)) for x in Lo]
    assert Lo == e.ii(ob + 0x60, 3), (Lo, e.ii(ob + 0x60, 3))
    scr = []
    for k, v in enumerate(verts):
        z = s32(W[6] * v[0] + W[7] * v[1] + W[8] * v[2] + T[2])
        rec = e.ii(vl + 0x24 * k, 8)
        if z > 0x1900000:
            f = trunc_div(0xe00000, z >> 16)
            X = s32(W[0] * v[0] + W[1] * v[1] + W[2] * v[2] + T[0]); Y = s32(W[3] * v[0] + W[4] * v[1] + W[5] * v[2] + T[1])
            sx = s32((X * f) >> 27) + 0x1400; sy = s32((Y * f) >> 27) + 0xc80
            ok = rec[3:8] == [X, Y, z, sx, sy]
        else:
            sx = sy = None; ok = rec[5] == z
        scr.append((z, sx, sy)); tot += 1; bad += not ok
    for f in faces:
        P = [scr[q] for q in f['v']]
        vis = all(p[0] > 0x1900000 for p in P)
        if vis:
            dx1, dy1 = P[1][1] - P[0][1], P[1][2] - P[0][2]; dx2, dy2 = P[2][1] - P[0][1], P[2][2] - P[0][2]
            vis = s32(dy2 * dx1) > s32(dx2 * dy1)
        f['vis'] = vis
        if vis:
            f['col'] = (base := f['base'], (s32(f['n'][0] * Lo[0] + f['n'][1] * Lo[1] + f['n'][2] * Lo[2]) >> 12) + f['base'])[1] & 0xff
    for f in faces:
        fl_emu = e.rd(f['addr'] + 0x1d, 1)[0]; col_emu = e.rd(f['addr'] + 0x1f, 1)[0]
        tot += 1
        if bool(fl_emu & 0x80) == f['vis']: bad += 1
        elif f['vis'] and col_emu != f['col']: bad += 1
print('checked', tot, 'mismatches', bad)
f0 = faces
print('static flags (bits3-5) per face:', [hex(f['flags0'] & 0x38) for f in faces][:40])
print('neighbours:', [f['nb'] for f in faces][:16])
print('colours of visible faces', sorted(set(f.get('col') for f in faces if f['vis'])))
