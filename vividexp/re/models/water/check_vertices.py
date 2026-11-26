"""Check wmodel's placement, rotation, projection, environment coordinates and back-face flags against the vertex and
normal arrays the hooked run dumped at each object's draw (0x40580c), for every drawn frame of the part."""
import sys

import numpy as np

import wdata
import wlog
import wmodel

D = wdata.load()
F = wlog.frames()
# draws in order: frame index by (record of flip): rebuild per frame k the list of dumps
dumps = list(wlog.dumps())
pos = 0
nbad = {'pos': 0, 'scr': 0, 'env': 0, 'flag': 0, 'bump': 0}
checked = 0
first_bad = []
step = int(sys.argv[1]) if len(sys.argv) > 1 else 1
for fi, fr in enumerate(F):
    n = len(fr['draws'])
    group = dumps[pos:pos + n]
    pos += n
    if fr['counts'][0] is None or fi % step:
        continue
    for (rec, k, v, nr, fl) in group:
        t = wmodel.obj_t(D, k, fr['counts'][k])
        o = wmodel.transform(D, k, wmodel.place(D, k, t))
        x, y, z = o.pv[:3]
        wmodel.project(o, 0xd8f0, 0xb4c6, -7000, x, y, z, 160, 100)
        wmodel.backface(o)
        V = np.array(o.V)
        S = np.array(o.S[:-1])
        N = np.array(o.N)
        UV = np.array(o.uv)
        checked += 1
        bad = []
        if not (v[:, 4:7] == V).all():
            nbad['pos'] += 1; bad.append('pos')
        if not (v[:-1, 2:4] == S).all():
            nbad['scr'] += 1; bad.append('scr')
        if not (nr[:, 0:3] == N).all():
            nbad['env'] += 1; bad.append('env')
        if not (fl == np.array(o.flags)).all():
            nbad['flag'] += 1; bad.append('flag')
        if not ((v[:-1, 0:2].astype(np.int64) & 0xffff) == UV).all():
            nbad['bump'] += 1; bad.append('bump')
        if bad and len(first_bad) < 5:
            first_bad.append((fi, rec, k, bad, fr['counts']))
print('checked', checked, 'object draws; mismatches', nbad)
for b in first_bad:
    print('  ', b)
