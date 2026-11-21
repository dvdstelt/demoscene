"""The interpolated level of the pipe rasterizer 0x416dc8 and the lit rasterizer 0x414e5c (ECX >> 8 at the code
hooks of run_temple.py --pixframes) against floor(value / 16) of the vertex values (+0x30) interpolated affinely or
perspective-correctly at the pixel centre, on faces of at least 40 px (pipes) / 300 px (hall) that do not cross the
near plane. "inner" = pixels whose barycentric weights are all above 0.05.
usage: levels.py out/t2.pkl"""
import sys, collections
import numpy as np
import dumpread as D

MIN_AREA = {0x416dc8: 40, 0x414e5c: 300}

for rec in [r for r in D.load(sys.argv[1]) if 'lv' in r]:
    m = D.Blocks(rec)
    fn = {f[0]: f[2] for f in rec['faces']}
    byface = collections.defaultdict(list)
    for f, addr, off, ecx, acc in rec['lv']:
        byface[f].append((off % 320, off // 320, (ecx >> 8) & 255))
    for kind in (0x416dc8, 0x414e5c):
        rows = []
        for f, pts in byface.items():
            if fn[f] != kind:
                continue
            vs = [m.d32(f + k) for k in (0x14, 0x18, 0x1c)]
            P = np.array([[m.i32(v + 0x14) / 16, m.i32(v + 0x18) / 16] for v in vs])
            Z = np.array([m.i32(v + 0x1c) for v in vs], float)
            L = np.array([m.i16(v + 0x30) for v in vs], float)
            A = np.array([P[:, 0], P[:, 1], [1, 1, 1]])
            if abs(np.linalg.det(A)) / 2 < MIN_AREA[kind] or (Z < 8192).any():
                continue
            Ai = np.linalg.inv(A)
            for x, y, lv in pts:
                b = Ai @ np.array([x + 0.5, y + 0.5, 1])
                w = b / Z
                rows.append((lv, b @ L, (w @ L) / w.sum(), b.min()))
        if not rows:
            continue
        v = np.array(rows)
        inner = v[:, 3] > 0.05
        for col, name in ((1, 'affine'), (2, 'perspective')):
            e = v[:, 0] - np.floor(v[:, col] / 16)
            print('%d %s %-11s pixels %5d: exact %.3f, within 1 %.3f | inner %5d: exact %.3f, within 1 %.3f' % (
                rec['frame'], hex(kind), name, len(v), (e == 0).mean(), (np.abs(e) <= 1).mean(),
                inner.sum(), (e[inner] == 0).mean(), (np.abs(e[inner]) <= 1).mean()))
