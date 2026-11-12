"""Back-face test of the engine (3377) in p2: the 2D cross product is computed with 16-bit imul, so faces whose
doubled screen area exceeds 32767 px^2 can get the wrong sign. For each dump, count the faces where the 16-bit
sign differs from the true one, split into wrongly drawn (true back face) and wrongly dropped (true front face),
and the screen pixels these faces cover."""
import glob, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mesh import mesh
from project import project, frame_state, raster


def s16(v): return ((v + 0x8000) & 0xffff) - 0x8000


for p in sorted(glob.glob(os.path.join(HERE, 'out', 'runs', 'p2_dump_*.npz')), key=lambda s: int(s.split('_')[-1][:-4])):
    d = np.load(p)
    cam, var = frame_state(os.path.join(HERE, 'out', 'runs', 'p2.p2log'), int(d['drawn']))
    x, y, z, vx, vy, vz, a, b, c, va, vb, vc = cam
    out = []
    piv = 0
    for line in open(os.path.join(HERE, 'out', 'runs', 'p2.p2log')):
        if line.startswith('R %d ' % int(d['ret'])): piv = int(line.split()[15]); break
    for name, seg, f, show in (('discs', 0x1e71, 4000, True), ('C', 0x2121, 1300, var['Chidden'] == 0)):
        if not show: continue
        m = mesh(seg); F = m['F'][:, 1:] // 4
        px, py, Z = project(m['V'], (a, b, c), (x, y, z), (piv if name == 'discs' else 0, 0, 0), f)
        x0, x1, x2 = px[F[:, 0]], px[F[:, 1]], px[F[:, 2]]
        y0, y1, y2 = py[F[:, 0]], py[F[:, 1]], py[F[:, 2]]
        true = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        c16 = s16(s16(s16(x1 - x0) * s16(y2 - y0)) - s16(s16(x2 - x0) * s16(y1 - y0)))
        drawn_wrong = (true < 0) & (c16 >= 0); dropped_wrong = (true >= 0) & (c16 < 0)
        cov_d = raster(px, py, F[drawn_wrong]).sum() if drawn_wrong.any() else 0
        cov_x = raster(px, py, F[dropped_wrong]).sum() if dropped_wrong.any() else 0
        out.append('%s: %d back faces drawn (%d px), %d front faces dropped (%d px)' % (
            name, drawn_wrong.sum(), cov_d, dropped_wrong.sum(), cov_x))
    print(os.path.basename(p), int(d['ret']), '; '.join(out))
