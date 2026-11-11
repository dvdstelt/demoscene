import pickle, numpy as np, sys
import mm
from render import R
from cmp1 import MM, render_scene
dumps = pickle.load(open(sys.argv[1], 'rb'))
def idiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q
def s32(v): return ((v + 2**31) % 2**32) - 2**31
for i, d in enumerate(dumps):
    if d['addr'] != 0x610a or 'mem' not in d: continue
    s = MM(d['mem'])
    P = s.d(0x5de4)
    nv = s.d(P+0x6c); va = s.d(P+0x78); nf = s.d(P+0x70); fa = s.d(P+0x74)
    Lx, Ly, Lz = s.i(0x1e48c), s.i(0x1e490), s.i(0x1e494)
    cx, cy, cz = s.i(0x1ea9d), s.i(0x1eaa1), s.i(0x1eaa5)
    ebx = idiv(0xe00000, -cz)
    sh = {}
    for v in range(nv):
        p = va + v*0x24
        t = idiv((-Lz) << 16, (s.i(p+0x14) >> 16) - Lz + cz)
        X = s32(((s.i(p+0xc) >> 16) - Lx + cx) * t) + (Lx << 16)
        Y = s32(((s.i(p+0x10) >> 16) - Ly + cy) * t) + (Ly << 16)
        X = s32(X); Y = s32(Y)
        sxv = ((X * ebx) >> 32) + 160
        syv = ((Y * ebx) >> 32) + 100
        sh[p] = (sxv, syv)
        # compare with stored words
        import struct
        wx, wy = struct.unpack_from('<hh', s.b, p + 0x20)
        assert (wx, wy) == (sxv, syv), (v, wx, wy, sxv, syv)
    tex = np.frombuffer(s.b, np.uint8, 64000, s.d(0x1edf5)).copy()
    r = R(s)
    for f in range(nf):
        fp = fa + f*0x4c
        pts = [sh[s.d(fp + 4*k)] for k in range(3)]
        r.tri(pts, 0x20, op='or', target=tex)
    ref = np.frombuffer(s.b, np.uint8, 64000, s.d(8))
    print('shadow texture match', (tex == ref).mean())
    # composite + pencil
    scene = np.frombuffer(dumps[i-1]['buf'] if dumps[i-1]['addr']==0x604a else None, np.uint8)
    # scene after 5eec = dump 604a of the same frame; composite
    T = np.frombuffer(s.b, np.uint8, 0x10000, s.d(0x1eded)).reshape(256,256)
    comp = T[tex, scene].copy()
    r2 = R(s); r2.buf[1024:1024+64000] = comp
    def vz(v): return s.i(v+0x14) >> 16
    buckets = {}
    for f in range(nf):
        fp = fa + f*0x4c
        if s.by(fp+0x1d) & 0x80: continue
        vs = [s.d(fp + 4*k) for k in range(3)]
        dd = min(max((sum(vz(v) for v in vs) - 0x4b0) >> 6, 0), 0x3ff)
        buckets.setdefault(dd, []).insert(0, fp)
    for dd in sorted(buckets, reverse=True):
        for fp in buckets[dd]:
            pts = [(s.i(s.d(fp+4*k)+0x18) >> 5, s.i(s.d(fp+4*k)+0x1c) >> 5) for k in range(3)]
            r2.tri(pts, s.by(fp+0x1f))
    fin = r2.img()
    nxt = [x for x in dumps[i+1:] if x['addr'] == 0x611a]
    if nxt:
        ref2 = np.frombuffer(nxt[0]['buf'], np.uint8).reshape(200,320)
        print('final frame match', (fin == ref2).mean(), (fin != ref2).sum())
    print('pencil face colours', sorted(set(s.by(fa+f*0x4c+0x1f) for f in range(nf) if not s.by(fa+f*0x4c+0x1d)&0x80)))
