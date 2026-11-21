"""Survey of the flower objects kukka*.sro: counts, extents, how the morph targets correspond to kukka1."""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sro  # noqa: E402

names = ['kukka1', 'kukka1-2', 'kukka1-3', 'kukka1-4', 'kukka1-5', 'kukka1-6', 'kukka1-7', 'kukka1-8', 'kukka2',
         'kukka2-2']
d = sro.dat()
# the two kukka1.sro entries in INSIDE.DAT
ents = [k for k in d.files] if isinstance(d.files, dict) else None
objs = {n: sro.load('flower/%s.sro' % n) for n in names}
v1, f1 = objs['kukka1']
P1 = np.stack([v1['x'], v1['y'], v1['z']], 1).astype(float)
for n in names:
    v, f = objs[n]
    P = np.stack([v['x'], v['y'], v['z']], 1).astype(float)
    m = min(len(P), len(P1))
    same_faces = len(f) == len(f1) and np.array_equal(f['a'], f1['a']) and np.array_equal(f['b'], f1['b'])
    r = np.linalg.norm(P, axis=1)
    print('%-9s %3d verts %3d faces  x %6d..%6d y %6d..%6d z %6d..%6d  |r| max %6.0f  same faces as kukka1: %s  '
          'mean |dP| to kukka1 %.0f  u %d..%d v %d..%d' % (
              n, len(v), len(f), P[:, 0].min(), P[:, 0].max(), P[:, 1].min(), P[:, 1].max(), P[:, 2].min(),
              P[:, 2].max(), r.max(), same_faces, np.linalg.norm(P[:m] - P1[:m], axis=1).mean(), v['u'].min(),
              v['u'].max(), v['v'].min(), v['v'].max()))
print('kukka1 first vertices', v1[:6])
print('kukka2 first vertices', objs['kukka2'][0][:6])
print('kukka1 faces', f1[:8])
print('kukka2 faces', objs['kukka2'][1][:8])
raw = d.get('flower/kukka1.sro')
