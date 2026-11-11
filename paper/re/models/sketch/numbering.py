import pickle
from cmp1 import MM
from mm import M
img = M('img')
dumps = pickle.load(open('d2000.dumps', 'rb'))
s = MM([d for d in dumps if d['addr']==0x604a and 'mem' in d][0]['mem'])
def s32(v): return ((v + 2**31) % 2**32) - 2**31
SIN = [s.i(0x22479 + 4*i) for i in range(1024)]
def rot(b, a):
    cb = SIN[(b+256)&1023]; sb = SIN[b&1023]; ca = SIN[(a+256)&1023]; sa = SIN[a&1023]
    return [cb, 0, -sb, s32((-sb*sa) >> 16), ca, s32(-((cb*sa) >> 16)), s32((sb*ca) >> 16), sa, s32((cb*ca) >> 16)]
def proj(Cam, campos, P):
    # objects have identity matrix and pos 0 -> M = Cam (via 0xae2 with identity: exact)
    pos = s.ints(P+0x48, 3); dd = [pos[k]-campos[k] for k in range(3)]
    pc = [s32(sum(Cam[r*3+k]*dd[k] for k in range(3))) for r in range(3)]
    Obj = s.ints(P, 9)
    Mx = [s32(sum(s32((Cam[r*3+k]*Obj[k*3+c]) >> 16) for k in range(3))) for r in range(3) for c in range(3)]
    out = {}
    va = s.d(P+0x78)
    for v in range(s.d(P+0x6c)):
        p = va + v*0x24; vv = s.ints(p, 3)
        z = s32(sum(Mx[6+k]*vv[k] for k in range(3)) + pc[2])
        if z > 0x1900000:
            e = 0xe00000 // (z >> 16)
            x = s32(sum(Mx[k]*vv[k] for k in range(3)) + pc[0]); y = s32(sum(Mx[3+k]*vv[k] for k in range(3)) + pc[1])
            out[p] = (z, s32((x*e) >> 27) + 0x1400, s32((y*e) >> 27) + 0xc80)
        else: out[p] = (z, None, None)
    return out
key0 = [img.i(0x5de8 + 4*k) for k in range(5)]
Cam = rot(key0[0], key0[1]); campos = [key0[2] >> 10, key0[3] >> 10, key0[4] >> 10]
print('setup camera', key0[:2], campos)
objs = [s.d(0x5db8 + 4*k) for k in range(11)]
ordinal = {}
n = 0
for group in ([0, 1], [2, 3, 4, 5, 6, 7], [9, 10]):
    buckets = {}
    for k in group:
        P = objs[k]; V = proj(Cam, campos, P)
        fa = s.d(P+0x74)
        for f in range(s.d(P+0x70)):
            fp = fa + f*0x4c
            vs = [V[s.d(fp+4*i)] for i in range(3)]
            if any(v[0] <= 0x1900000 for v in vs): continue
            dx1 = vs[1][1]-vs[0][1]; dy1 = vs[1][2]-vs[0][2]; dx2 = vs[2][1]-vs[0][1]; dy2 = vs[2][2]-vs[0][2]
            if not (s32(dy2*dx1) > s32(dx2*dy1)): continue
            d = min(max((sum(v[0] >> 16 for v in vs) - 0x4b0) >> 6, 0), 0x3ff)
            buckets.setdefault(d, []).insert(0, fp)
    for d in sorted(buckets, reverse=True):
        for fp in buckets[d]:
            ordinal[n] = fp; n += 1
print('faces numbered', n)
ok = 0
for e in range(0x113):
    a = 0x5073 + 5*e
    if ordinal.get(img.d(a)) == s.d(a): ok += 1
print('entries resolved identically', ok, 'of', 0x113)
