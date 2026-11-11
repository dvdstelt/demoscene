import numpy as np, sys
from mm import M
from render import R
s = M(2000); s3 = M(3300); img = M('img')
r = R(s)
objs = [s.d(0x5db8 + 4*k) for k in range(11)]
# sketch flags: entries 0..0x111 complete
flags = {}
for e in range(0x112):
    a = 0x5073 + 5*e
    fp = s.d(a); fl = img.by(a+4) & 7
    flags[fp] = flags.get(fp, 0) | fl | (fl << 3)
cur_face = s.d(0x5073 + 5*0x112)
def vxy(v): return (s.i(v+0x18) >> 5, s.i(v+0x1c) >> 5)
def vz(v): return s.i(v+0x14) >> 16
for group in ([0, 1], [2, 3, 4, 5, 6, 7, 8], [9, 10]):
    buckets = {}
    for k in group:
        o = objs[k]; fa = s.d(o+0x74); nf = s.d(o+0x70)
        for f in range(nf):
            fp = fa + f*0x4c
            if s.by(fp+0x1d) & 0x80: continue
            vs = [s.d(fp), s.d(fp+4), s.d(fp+8)]
            d = (sum(vz(v) for v in vs) - 0x4b0) >> 6
            d = min(max(d, 0), 0x3ff)
            buckets.setdefault(d, []).insert(0, fp)
    for d in sorted(buckets, reverse=True):
        for fp in buckets[d]:
            vs = [s.d(fp), s.d(fp+4), s.d(fp+8)]
            pts = [vxy(v) for v in vs]
            col = s.by(fp+0x1f)
            # mode 2 colour: base+shade (+0x1c) band change -> 0x3a
            r.tri(pts, col)
            al = s.by(fp+0x1d) & flags.get(fp, 0)
            if fp == cur_face:
                pass  # zero-length partial line
            if al & 9: r.line(*pts[0], *pts[1])
            if al & 0x12: r.line(*pts[1], *pts[2])
            if al & 0x24: r.line(*pts[2], *pts[0])
mine = r.img()
ref = np.frombuffer(s3.b, np.uint8, 64000, s3.d(0x5063)).reshape(200, 320)
eq = mine == ref
print('match fraction', eq.mean(), 'mismatches', (~eq).sum())
ys, xs = np.nonzero(~eq)
print(list(zip(ys[:20], xs[:20])))
np.save('final_mine.npy', mine); np.save('final_ref.npy', ref)
