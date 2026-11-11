import pickle
from cmp1 import MM
dumps = pickle.load(open('d2000.dumps', 'rb'))
def s32(v): return ((v + 2**31) % 2**32) - 2**31
for addr, mode in ((0x604a, 2), (0x610a, 1)):
    s = MM([d for d in dumps if d['addr']==addr and 'mem' in d][0]['mem'])
    objs = [s.d(0x5db8 + 4*k) for k in range(11)] if mode == 2 else [s.d(0x5de4)]
    bad = badv = bads = n = 0
    for P in objs:
        lo = s.ints(P+0x60, 3); nf = s.d(P+0x70); fa = s.d(P+0x74)
        for f in range(nf):
            fp = fa + f*0x4c
            vs = [s.d(fp+4*k) for k in range(3)]
            z = [s.i(v+0x14) for v in vs]
            hidden = any(zz <= 0x1900000 for zz in z)
            if not hidden:
                X = [s.i(v+0x18) for v in vs]; Y = [s.i(v+0x1c) for v in vs]
                dx1, dy1 = X[1]-X[0], Y[1]-Y[0]; dx2, dy2 = X[2]-X[0], Y[2]-Y[0]
                hidden = not (s32(dy2*dx1) > s32(dx2*dy1))
            if hidden != bool(s.by(fp+0x1d) & 0x80): badv += 1; continue
            if hidden: continue
            n += 1
            nn = s.ints(fp+0x24, 3)
            al = ((s32(sum(nn[k]*lo[k] for k in range(3))) >> 12) + s.by(fp+0x1e)) & 0xff
            band = al >> 5
            if mode == 2:
                al = (al + s.by(fp+0x1c)) & 0xff
                if (al >> 5) != band: al = 0x3a
            if al != s.by(fp+0x1f): bad += 1
            # silhouette bits
            sb = 0
            for k, off in enumerate((0xc, 0x10, 0x14)):
                nb = s.d(fp+off)
                if s.by(nb+0x1d) & 0x80: sb |= 1 << k
            if (s.by(fp+0x1d) & 7) != sb: bads += 1
    print('mode', mode, 'visible faces', n, 'visibility mismatches', badv, 'colour mismatches', bad, 'silhouette mismatches', bads)
