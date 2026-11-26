"""How far is the painter's order (face mid-z sort, object Z sort) from a depth buffer? Same coverage (the
original's rasterizer and culls), per-pixel nearest face by 1/depth interpolated in screen space vs the face drawn
last. Prints the number of pixels whose visible face differs.

usage: python3 painter_check.py gen/gen.ini t1 t2 ...
"""
import sys
import numpy as np
import render
import gen
from gen import s16


class Page:
    def __init__(self):
        self.last = np.full((200, 320), -1, np.int64)
        self.near = np.full((200, 320), -1, np.int64)
        self.w = np.zeros((200, 320))
        self.plane = None
        self.fid = -1

    def __setitem__(self, yx, val):
        y, x = yx
        self.last[y, x] = self.fid
        a, b, c = self.plane
        w = a * x + b * y + c
        if w > self.w[y, x]:
            self.w[y, x] = w
            self.near[y, x] = self.fid


def run(sc, t):
    pg = Page()
    per = []
    for i, o in enumerate(sc.objs):
        pos, vals = sc.state(o, t)
        v, nrm = render.transform(o, vals)
        sx, sy = render.project(v, vals[0], vals[1], vals[2])
        per.append((o, vals, v, sx, sy))
    order = render.isort_desc(list(range(len(sc.objs))), key=lambda i: per[i][1][2])
    fid = 0
    for i in order:
        o, vals, v, sx, sy = per[i]
        Z = vals[2]
        faces = o.m3d.groups[0][1]
        keys = []
        for fi, f in enumerate(faces):
            a, b, c = f[0], f[1], f[2]
            p1 = s16(sy[a] - sy[b]) * s16(sx[c] - sx[b])
            p2 = s16(sx[a] - sx[b]) * s16(sy[c] - sy[b])
            if p2 < p1 or not all(int(v[2][f[k]]) + Z > -6720 for k in range(3)):
                continue
            zs = sorted([int(v[2][f[0]]), int(v[2][f[1]]), int(v[2][f[2]])])
            keys.append((fi, s16(zs[0] + (s16(zs[2] - zs[0]) >> 1))))
        keys = render.isort_desc(keys, key=lambda k: k[1]) if len(keys) < 200 else render.hpsort_desc(keys, key=lambda k: k[1])
        for fi, _ in keys:
            f = faces[fi]
            pts = [(int(sx[f[k]]), int(sy[f[k]]), 1.0 / (7000 + int(v[2][f[k]]) + Z)) for k in range(3)]
            M = np.array([[p[0], p[1], 1.0] for p in pts])
            try:
                pg.plane = np.linalg.solve(M, np.array([p[2] for p in pts]))
            except np.linalg.LinAlgError:
                continue
            pg.fid = fid
            fid += 1
            vs = [(p[0], p[1], 0, 0, 0, 0) for p in pts]
            render.raster(pg, vs, lambda a, b, c, d: 0)
    return pg


sc = render.Scene(sys.argv[1])
for t in map(int, sys.argv[2:]):
    pg = run(sc, t)
    covered = pg.last >= 0
    diff = covered & (pg.last != pg.near)
    print('t %d: covered %d px, visible face differs from the depth-buffer one in %d px' % (t, covered.sum(), diff.sum()))
