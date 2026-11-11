"""Statistics of one render dump: faces dropped by the near test, culled, clipped; depth key range; z range.
usage: python3 stats.py DUMPDIR N"""
import os, sys, pickle
import numpy as np
import model_render as mr
d, n = sys.argv[1], int(sys.argv[2])
pre = pickle.load(open(os.path.join(d, 'r%04d.pre' % n), 'rb'))
m = mr.Mem(pre)
W, H = m.u32(0x18d64), m.u32(0x18d68)
verts, lst = mr.transform(m, W, H)
near = sum(1 for v in verts.values() if v.inside == 4)
zs = [float(v.z) for v in verts.values()]
keys = [k for k, _ in lst]
# faces dropped because a vertex is behind z = 16
OB = m.u32(0x18d50); FB = m.u32(0x18d48)
dropped = 0; total = 0
for o in range(m.u32(0x18d58)):
    ob = OB + o * 0x50
    if m.u32(ob + 0x4c) & 0x41: continue
    for k in range(m.u32(ob + 4)):
        fa = FB + (m.u32(ob) + k) * 0x1c
        total += 1
        if any(verts[m.u32(fa + 4 * i)].inside == 4 for i in range(3)): dropped += 1
clipped = sum(1 for _, f in lst if f['clip'])
print('W,H %d,%d  vertices %d (behind z=16: %d), z range %.0f..%.0f; faces %d, dropped by the near test %d, '
      'drawn %d (clipped %d); key range %d..%d' % (W, H, len(verts), near, min(zs), max(zs), total, dropped, len(lst),
                                                  clipped, min(keys), max(keys)))
# dropped faces that still have a vertex in front of the near plane and on screen (these leave visible gaps)
vis = 0
for o in range(m.u32(0x18d58)):
    ob = OB + o * 0x50
    if m.u32(ob + 0x4c) & 0x41: continue
    for k in range(m.u32(ob + 4)):
        fa = FB + (m.u32(ob) + k) * 0x1c
        vs = [verts[m.u32(fa + 4 * i)] for i in range(3)]
        if any(v.inside == 4 for v in vs) and any(v.inside == 1 for v in vs): vis += 1
print('  dropped faces with a vertex on screen: %d' % vis)
# area of the screen that the dropped faces would cover if they were clipped at z = 16 instead (rasterised with
# numpy at 320x200, pixel centres, ignoring the painter's order): an upper bound of the visible gaps
F = float(m.f(OB + 0x40)); Wd, Hd = W, H
ys, xs = np.mgrid[0:Hd, 0:Wd] + 0.5
cover = np.zeros((Hd, Wd), bool)
def cam(v):
    # camera-space coordinates (x' and y' were scaled by 65536*W/320 and -H/200 in the transform)
    return np.array([float(v.ox), float(v.oy), float(v.oz)])
for o in range(m.u32(0x18d58)):
    ob = OB + o * 0x50
    if m.u32(ob + 0x4c) & 0x41: continue
    mat = [float(m.f(ob + 0x10 + 4 * i)) for i in range(12)]
    R = np.array(mat[:9]).reshape(3, 3); tv = np.array(mat[9:12]); tv[2] += F
    for k in range(m.u32(ob + 4)):
        fa = FB + (m.u32(ob) + k) * 0x1c
        vs = [verts[m.u32(fa + 4 * i)] for i in range(3)]
        if not any(v.inside == 4 for v in vs) or all(v.inside == 4 for v in vs): continue
        P = [R @ cam(v) + tv for v in vs]
        poly = []
        for i in range(3):
            a, b = P[i], P[(i + 1) % 3]
            if a[2] >= 16: poly.append(a)
            if (a[2] >= 16) != (b[2] >= 16):
                s = (16 - a[2]) / (b[2] - a[2]); poly.append(a + s * (b - a))
        q = [(Wd / 2 + Wd / 320 * F * p[0] / p[2], Hd / 2 - Hd / 200 * F * p[1] / p[2]) for p in poly]
        for i in range(1, len(q) - 1):
            (x0, y0), (x1, y1), (x2, y2) = q[0], q[i], q[i + 1]
            d = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if d == 0: continue
            w0 = ((x1 - xs) * (y2 - ys) - (x2 - xs) * (y1 - ys)) / d
            w1 = ((x2 - xs) * (y0 - ys) - (x0 - xs) * (y2 - ys)) / d
            cover |= (w0 >= 0) & (w1 >= 0) & (w0 + w1 <= 1)
print('  pixels covered by near-dropped faces if they were clipped at z = 16: %d' % int(cover.sum()))
