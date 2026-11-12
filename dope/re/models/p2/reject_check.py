"""The engine filler's trivial reject (2881): a triangle is skipped when all three vertices have their packed
(x << 16 | y) above 0x01400000 unsigned, i.e. x >= 320 or x < 0 for every vertex (also when they lie on both sides
of the screen), or when its top y > 201 or bottom y < 1. Apply it to the projection model and compare the coverage
with the engine's render buffer in the dumps (cf. project.py, which does not apply it)."""
import glob, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mesh import mesh
from project import project, frame_state, raster, img


def s16(v): return ((v + 0x8000) & 0xffff) - 0x8000


for p in sorted(glob.glob(os.path.join(HERE, 'out', 'runs', 'p2_dump_*.npz')), key=lambda s: int(s.split('_')[-1][:-4])):
    d = np.load(p)
    cam, var = frame_state(os.path.join(HERE, 'out', 'runs', 'p2.p2log'), int(d['drawn']))
    x, y, z, vx, vy, vz, a, b, c, va, vb, vc = cam
    ret = int(d['ret'])
    piv = 0
    for line in open(os.path.join(HERE, 'out', 'runs', 'p2.p2log')):
        if line.startswith('R %d ' % ret): piv = int(line.split()[15]); break
    cov_all = np.zeros((202, 320), bool); cov_kept = np.zeros((202, 320), bool); nrej = 0
    for name, seg, f, show in (('discs', 0x1e71, 4000, True), ('C', 0x2121, 1300, var['Chidden'] == 0)):
        if not show: continue
        m = mesh(seg); F = m['F'][:, 1:] // 4
        px, py, Z = project(m['V'], (a, b, c), (x, y, z), (piv if name == 'discs' else 0, 0, 0), f)
        X = [px[F[:, k]] for k in range(3)]; Y = [py[F[:, k]] for k in range(3)]
        cross = s16(s16(s16(X[1] - X[0]) * s16(Y[2] - Y[0])) - s16(s16(X[2] - X[0]) * s16(Y[1] - Y[0])))
        front = cross >= 0
        out = [((X[k] & 0xffff) > 0x140) | (((X[k] & 0xffff) == 0x140) & ((Y[k] & 0xffff) > 0)) for k in range(3)]
        rej = out[0] & out[1] & out[2]
        ys = np.stack(Y); rej |= (ys.min(0) > 0xc9) | (ys.max(0) < 1)
        cov_all |= raster(px, py, F[front])
        cov_kept |= raster(px, py, F[front & ~rej])
        nrej += int((front & rej).sum())
    new = d['new'][:202 * 320].reshape(202, 320)
    bg = np.zeros((202, 320), np.uint8)
    if var['backdrop']:
        bg[1:201] = np.frombuffer(img[0xec30:0xec30 + 64000], np.uint8).reshape(200, 320)
    eng = new != bg
    rows = np.ones(202, bool); rows[0] = rows[201] = False
    if var['strip'] != -2:
        r0 = var['row'] // 320; rows[r0:r0 + 32] = False
    e = eng[rows]
    for nm, cv in (('no reject', cov_all[rows]), ('with reject', cov_kept[rows])):
        print('%s ret %d %-11s: only engine %5d, only model %5d' % (os.path.basename(p), ret, nm, (e & ~cv).sum(),
                                                                    (cv & ~e).sum()), end='   ')
    print('front faces rejected', nrej)
