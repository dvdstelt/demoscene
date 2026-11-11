import pickle, numpy as np, sys, math, struct
from cmp1 import MM
dumps = pickle.load(open(sys.argv[1] if len(sys.argv) > 1 else 'd2000.dumps', 'rb'))
def s32(v): return ((v + 2**31) % 2**32) - 2**31
def idiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q
def check(s, obj_ptrs, cam_ang, campos, label, objang=None):
    SIN = [s.i(0x22479 + 4*i) for i in range(1024)]
    def rot(b, a):
        cb = SIN[(b+256)&1023]; sb = SIN[b&1023]; ca = SIN[(a+256)&1023]; sa = SIN[a&1023]
        return [cb, 0, -sb,
                s32((-sb*sa) >> 16), ca, s32(-((cb*sa) >> 16)),   # note: (-sb)*sa>>16 floor
                s32((sb*ca) >> 16), sa, s32((cb*ca) >> 16)]
    Cam = rot(*cam_ang)
    camC = s.ints(0x1e468, 9)
    print(label, 'cam matrix ok', Cam == camC)
    L = [s.i(0x1e48c), s.i(0x1e490), s.i(0x1e494)]
    bad = 0
    for P in obj_ptrs:
        Obj = s.ints(P, 9)
        if objang is not None:
            print('  obj matrix from angles ok', rot(*objang) == Obj)
        M = [0]*9
        for r_ in range(3):
            for c in range(3):
                M[r_*3+c] = s32(sum(s32((Cam[r_*3+k]*Obj[k*3+c]) >> 16) for k in range(3)))
        assert M == s.ints(P+0x24, 9), 'M'
        pos = s.ints(P+0x48, 3)
        dd = [pos[k] - campos[k] for k in range(3)]
        pc = [s32(sum(Cam[r_*3+k]*dd[k] for k in range(3))) for r_ in range(3)]
        assert pc == s.ints(P+0x54, 3), ('pc', pc, s.ints(P+0x54,3))
        l = [L[k] - pos[k] for k in range(3)]
        lo = [s32(sum(Obj[k*3+c]*l[k] for k in range(3))) >> 16 for c in range(3)]
        n = math.sqrt(sum(x*x for x in lo)) / 200.0
        lo = [int(round(x / n)) for x in lo]
        assert lo == s.ints(P+0x60, 3), ('lo', lo, s.ints(P+0x60,3))
        nv = s.d(P+0x6c); va = s.d(P+0x78)
        for v in range(nv):
            p = va + v*0x24
            vv = s.ints(p, 3)
            z = s32(sum(M[6+k]*vv[k] for k in range(3)) + pc[2])
            if z != s.i(p+0x14): bad += 1; continue
            if z > 0x1900000:
                e = 0xe00000 // (z >> 16)
                x = s32(sum(M[k]*vv[k] for k in range(3)) + pc[0])
                y = s32(sum(M[3+k]*vv[k] for k in range(3)) + pc[1])
                sx = s32((x*e) >> 27) + 0x1400
                sy = s32((y*e) >> 27) + 0xc80
                if (sx, sy) != (s.i(p+0x18), s.i(p+0x1c)): bad += 1
    print(label, 'vertex mismatches', bad)
for d in dumps:
    if d['addr'] == 0x604a and 'mem' in d:
        s = MM(d['mem']); break
objs = [s.d(0x5db8 + 4*k) for k in range(11)]
check(s, objs, (20, -28), (-704, 576, -1600), 'scene')
SIN = [s.i(0x22479 + 4*i) for i in range(1024)]
exact = [int(round(65536*math.sin(2*math.pi*i/1024))) for i in range(1024)]
print('sine table diffs vs round(65536 sin(2 pi i/1024))', sum(a != b for a, b in zip(SIN, exact)), max(abs(a-b) for a,b in zip(SIN,exact)))
print('sin float const', struct.unpack('<f', s.b[0x1e464:0x1e468])[0], math.pi/1024)
for d in dumps:
    if d['addr'] == 0x610a and 'mem' in d:
        s = MM(d['mem']); break
P = s.d(0x5de4)
x = s.i(0x55d6); a = 2*s.i(0x55d2) + 10; b = a + 0x5a - ((x - 0xa0) >> 1)
print('pencil x,y,z', x, s.i(0x55da), s.i(0x55de), 'a,b', a, b, 'pos', s.ints(P+0x48, 3))
check(s, [P], (0, 0), (0, 0, -2000), 'pencil', objang=(b, a))
