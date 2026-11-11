import pickle, numpy as np, sys
import mm
from render import R
class MM(mm.M):
    def __init__(self, b): self.b = b; self.pal = None
def render_scene(s, partial=True):
    r = R(s)
    objs = [s.d(0x5db8 + 4*k) for k in range(11)]
    def vxy(v): return (s.i(v+0x18) >> 5, s.i(v+0x1c) >> 5)
    def vz(v): return s.i(v+0x14) >> 16
    cur = s.d(0x5047)
    for group in ([0, 1], [2, 3, 4, 5, 6, 7, 8], [9, 10]):
        buckets = {}
        for k in group:
            o = objs[k]; fa = s.d(o+0x74); nf = s.d(o+0x70)
            for f in range(nf):
                fp = fa + f*0x4c
                if s.by(fp+0x1d) & 0x80: continue
                vs = [s.d(fp), s.d(fp+4), s.d(fp+8)]
                dd = (sum(vz(v) for v in vs) - 0x4b0) >> 6
                dd = min(max(dd, 0), 0x3ff)
                buckets.setdefault(dd, []).insert(0, fp)
        for dd in sorted(buckets, reverse=True):
            for fp in buckets[dd]:
                vs = [s.d(fp), s.d(fp+4), s.d(fp+8)]
                pts = [vxy(v) for v in vs]
                r.tri(pts, s.by(fp+0x1f))
                al = s.by(fp+0x1d) & s.by(fp+0x20)
                if partial and fp == cur:
                    r.line(s.i(0x55d6), s.i(0x55da), s.i(0x5053), s.i(0x5057))
                if al & 9: r.line(*pts[0], *pts[1])
                if al & 0x12: r.line(*pts[1], *pts[2])
                if al & 0x24: r.line(*pts[2], *pts[0])
    return r.img()
if __name__ == '__main__':
  dumps = pickle.load(open(sys.argv[1], 'rb'))
  for d in dumps:
    if d['addr'] == 0x604a and 'mem' in d:
        s = MM(d['mem'])
        mine = render_scene(s)
        ref = np.frombuffer(d['buf'], np.uint8).reshape(200, 320)
        print('tick', d['tick'], 'match', (mine == ref).mean(), (mine != ref).sum())
        np.save('m_%d.npy' % d['tick'], mine); np.save('r_%d.npy' % d['tick'], ref)
