"""Check the point light 0x41b508 on the logo object: vertex +0x30 = trunc(1600 (1 - d/550) (L - v).n / (d 512)), L = 0, range 550."""
import pickle, struct, sys
import numpy as np
ev = pickle.load(open(sys.argv[1], 'rb'))
draws = [e for e in ev if e['kind'] == 'f_draw']
allv = {}
for e in draws:
    o = e['obj']
    V = np.array([struct.unpack_from('<16i', o, 0x10c + 0x40 * i) for i in range(130)])
    for k in range(130):
        if V[k, 1] & 2:
            allv.setdefault(k, set()).add(int(np.int16(V[k, 12] & 0xffff)))
print('vertices lit at least once:', len(allv), ' with more than one value:', sum(len(s) > 1 for s in allv.values()))
o = draws[0]['obj']
V = np.array([struct.unpack_from('<16i', o, 0x10c + 0x40 * i) for i in range(130)])
v = V[:, 2:5].astype(float); n = V[:, 13:16].astype(float)
d = np.sqrt((v ** 2).sum(1)); dot = (-v * n).sum(1)
pred = np.where((dot >= 0) & (d <= 550), np.trunc((1600 - d * (1600 / 550)) * (dot / (d * 512))), 0)
pred = np.minimum(pred, 992)
bad = 0
for k, s in sorted(allv.items()):
    if pred[k] not in s:
        bad += 1
        print('mismatch v%d pred %d got %s' % (k, pred[k], s))
print('mismatches', bad, 'of', len(allv))
print('normal lengths', np.sqrt((n ** 2).sum(1)).min(), np.sqrt((n ** 2).sum(1)).max())
print('light range of all vertices: %d..%d' % (pred.min(), pred.max()))
print('outer vertices (r>400) light', np.unique(pred[d > 400]), ' inner', np.unique(pred[d < 400]))
