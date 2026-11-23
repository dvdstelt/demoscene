"""Check how the engine's face and vertex normals of obuke.sro relate to the geometry, and R = S^k per frame."""
import pickle, struct, sys
import numpy as np
from sro import load
ev = pickle.load(open(sys.argv[1], 'rb'))
draws = [e for e in ev if e['kind'] == 'f_draw']
e = draws[0]
o, base = e['obj'], e['obj_addr']
V = np.array([struct.unpack_from('<16i', o, 0x10c + 0x40 * i) for i in range(130)])
Fr = np.array([struct.unpack_from('<13i', o, 0x10c + 130 * 0x40 + 0x34 * i) for i in range(256)])
s = load()
P = s['V'][:, :3].astype(float); F = s['F'][:, 1:4]
vbase = base + 0x10c
idx = (Fr[:, 5:8] - vbase) // 0x40
print('face vertex indices equal file order:', (idx == F).all())
fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
fn /= np.linalg.norm(fn, axis=1)[:, None]
dumped_fn = Fr[:, 8:11].astype(float)
print('dumped face normal lengths', np.linalg.norm(dumped_fn, axis=1).min(), np.linalg.norm(dumped_fn, axis=1).max())
for sgn in (1, -1):
    for scale in (1024, 1023):
        pred = np.trunc(sgn * fn * scale)
        print(' face normal = trunc(%+d * %d * unit cross(b-a, c-a)): max diff %.0f' % (sgn, scale, np.abs(pred - dumped_fn).max()))
vn = V[:, 13:16].astype(float)
acc = np.zeros((130, 3)); acc2 = np.zeros((130, 3))
for f in range(256):
    for k in F[f]:
        acc[k] += dumped_fn[f]; acc2[k] += fn[f]
for name, a in (('sum of dumped face normals', acc), ('sum of unit face normals', acc2)):
    u = a / np.linalg.norm(a, axis=1)[:, None]
    for sgn in (1, -1):
        for scale in (819, 819.2, 820):
            print(' vertex normal = trunc(%+d %.1f normalize(%s)): max diff %.0f' % (sgn, scale, name, np.abs(np.trunc(sgn * scale * u) - vn).max()))
print('example vn', vn[:3], 'face normal flags +0x30', np.unique(Fr[:, 12], return_counts=True))
# R = S^k ?
S = None
for e in ev:
    if e['kind'] == 'tick':
        S = np.array(e['mat']).reshape(3, 3); break
from numpy.linalg import matrix_power
th = np.arccos((np.trace(S) - 1) / 2)
ks = []
for e in draws[:5] + draws[300:303] + draws[-3:]:
    M = np.array(struct.unpack_from('<9d', e['obj'], 0x28)).reshape(3, 3)
    k = np.arccos(np.clip((np.trace(M) - 1) / 2, -1, 1)) / th
    kk = int(round(k)); err = np.abs(matrix_power(S, kk) - M).max()
    print('draw n=%d refresh %d T %d: angle/step %.4f  ||S^%d - M|| %.2e' % (e['n'], e['refresh'], e['T'], k, kk, err))
