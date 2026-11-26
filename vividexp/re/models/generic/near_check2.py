"""Per object near the eye: how many of its faces are drawn / dropped by the near test, its depth range and screen extent."""
import sys
import numpy as np
import render
import gen

ini, t0, t1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
sc = render.Scene(ini)
for t in range(t0, t1):
    for i, o in enumerate(sc.objs):
        pos, vals = sc.state(o, t)
        v, nrm = render.transform(o, vals)
        depth = 7000 + v[2] + vals[2]
        if depth.min() > 1500:
            continue
        sx, sy = render.project(v, vals[0], vals[1], vals[2])
        drop = sum(1 for fc in o.m3d.groups[0][1] if min(depth[fc[0]], depth[fc[1]], depth[fc[2]]) <= 280)
        inf = depth > 280
        print('t %d obj %d depth %d..%d  faces dropped %d/160  screen x of vertices in front: %s..%s' % (
            t, i, depth.min(), depth.max(), drop, sx[inf].min() if inf.any() else '-', sx[inf].max() if inf.any() else '-'))
