"""The pipes' bounding boxes: 0x41172c (translate) recomputes the box, 0x4116c8 (scale) does not, so the engine culls
the pipes 3, 4, 5 with the box of the unscaled pipe (0x41a5b8: culled when all 8 corners share a clip flag). Over a
--lite dump of the whole part: in how many frames is a pipe culled while some of its real (scaled) vertices are in
front of the camera and on screen?  usage: bbox.py out/full_lite.pkl"""
import sys, itertools
import numpy as np
import dumpread as D

FX, FY = 21760 / 128, 18133 / 128


def flags(pc):
    if pc[2] * 128 <= 16384: return 0x100000
    x = 160 + FX * pc[0] / pc[2]; y = 100 + FY * pc[1] / pc[2]
    return (0x10000 if x < 0 else 0) | (0x20000 if x >= 320 else 0) | (0x40000 if y < 0 else 0) | (0x80000 if y >= 200 else 0)


d = D.load(sys.argv[1])
culled = {3: 0, 4: 0, 5: 0}; wrong = {3: [], 4: [], 5: []}
for rec in d:
    m = D.Blocks(rec)
    cam = m.d32(0x425e74)
    Cw = m.mat(cam + 0x70); cpos = np.array(m.ints(cam + 0x1c, 3), float)
    for oi in (3, 4, 5):
        o = m.d32(0x425de0 + 4 * oi)
        Mw = m.mat(o + 0x70); pos = np.array(m.ints(o + 0x1c, 3), float)
        lo, hi = m.ints(o + 0xe0, 3), m.ints(o + 0xec, 3)
        fa = 0xffffffff
        for c in itertools.product(*zip(lo, hi)):
            fa &= flags(Cw.T @ (Mw @ np.array(c, float) + pos - cpos))
        if not fa: continue
        culled[oi] += 1
        v0 = m.d32(o + 0xd0)
        vis = 0
        for k in range(15):
            pc = Cw.T @ (Mw @ np.array(m.ints(v0 + 0x40 * k + 8, 3), float) + pos - cpos)
            vis += flags(pc) == 0
        if vis: wrong[oi].append(rec['frame'])
print('frames', len(d))
for oi in (3, 4, 5):
    w = wrong[oi]
    print('pipe %d: box %s..%s culled in %d frames; of these, %d frames with real vertices on screen %s' % (
        oi, D.Blocks(d[0]).ints(D.Blocks(d[0]).d32(0x425de0 + 4 * oi) + 0xe0, 3), D.Blocks(d[0]).ints(D.Blocks(d[0]).d32(0x425de0 + 4 * oi) + 0xec, 3),
        culled[oi], len(w), (w[:6], w[-3:]) if w else ''))
