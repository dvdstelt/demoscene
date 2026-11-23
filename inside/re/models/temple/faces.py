"""Face set and draw order of a dumped frame (run_temple.py --dump, full): back-face culling (drawn faces = faces
whose normal points to the camera, minus those entirely off screen or behind the near plane) and the sort keys
(mesh face: trunc((z0 + z1 + z2) / 48), z in 1/128 units = mean depth x 8; flare: trunc(z128) / 16; descending).
usage: faces.py out/t2.pkl"""
import sys
import numpy as np
import dumpread as D

for rec in D.load(sys.argv[1])[:3]:
    m = D.Blocks(rec)
    drawn = {f for f, k, fn in rec['faces']}
    keys = [k for f, k, fn in rec['faces']]
    print(rec['frame'], 'items', len(keys), 'keys descending:', all(keys[i] >= keys[i + 1] for i in range(len(keys) - 1)))
    cam = m.d32(0x425e74)
    Cw = m.mat(cam + 0x70); cpos = np.array(m.ints(cam + 0x1c, 3), float)
    for oi in range(6):
        o = m.d32(0x425de0 + 4 * oi)
        Mw = m.mat(o + 0x70); pos = np.array(m.ints(o + 0x1c, 3), float)
        cobj = Mw.T @ (cpos - pos)
        f0 = m.d32(o + 0xd8); nf = m.i32(o + 0xdc)
        front = drawn_front = drawn_back = keyok = nd = 0
        for k in range(nf):
            f = f0 + 0x34 * k
            n = np.array(m.ints(f + 0x20, 3), float)
            p0 = np.array(m.ints(m.d32(f + 0x14) + 8, 3), float)
            isf = n @ (cobj - p0) > 0
            front += isf
            if f in drawn:
                nd += 1; drawn_front += isf; drawn_back += not isf
                z = sum(m.i32(m.d32(f + q) + 0x1c) for q in (0x14, 0x18, 0x1c))
                keyok += m.i32(f + 4) == int(z / 48)
        print('  object %d: faces %d, front-facing %d, drawn %d (front %d, back %d), key = trunc(sum z / 48) for %d' % (
            oi, nf, front, nd, drawn_front, drawn_back, keyok))
    fl = [m.d32(0x425e78 + 4 * i) for i in range(10)]
    print('  flares: key = trunc(z128) / 16 for', sum(m.i32(o + 0xc8 + 4) == int(m.i32(o + 0xc0) / 16) for o in fl), 'of 10')
