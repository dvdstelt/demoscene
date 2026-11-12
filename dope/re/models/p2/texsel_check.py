"""Which env map half does each object use? For the dumps, sample the render buffer at each projected vertex and
compare with the env map texel addressed by that vertex's rotated normal (col = 64 + (n.M0 >> 24),
row = 64 + (n.M1 >> 24)), for the left (6538) and the right (6540) half of the 256x128 picture."""
import glob, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mesh import mesh
from project import project, matrix, frame_state, img

env = np.frombuffer(img[0x65380:0x65380 + 0x8000], np.uint8).reshape(128, 256)
for p in sorted(glob.glob(os.path.join(HERE, 'out', 'runs', 'p2_dump_*.npz')), key=lambda s: int(s.split('_')[-1][:-4])):
    d = np.load(p)
    cam, var = frame_state(os.path.join(HERE, 'out', 'runs', 'p2.p2log'), int(d['drawn']))
    x, y, z, vx, vy, vz, a, b, c, va, vb, vc = cam
    new = d['new'][:202 * 320].reshape(202, 320)
    res = []
    piv = 0
    for line in open(os.path.join(HERE, 'out', 'runs', 'p2.p2log')):
        if line.startswith('R %d ' % int(d['ret'])): piv = int(line.split()[15]); break
    for name, seg, f, hidden in (('discs', 0x1e71, 4000, False), ('C', 0x2121, 1300, var['Chidden'] != 0)):
        if hidden: continue
        m = mesh(seg)
        M = matrix(a, b, c)
        N = m['N'].astype(np.int64)
        col = ((((N @ M[0]) >> 16) + 0x4000) & 0xffff) >> 8
        row = ((((N @ M[1]) >> 16) + 0x4000) & 0xffff) >> 8
        px, py, Z = project(m['V'], (a, b, c), (x, y, z), (0, 0, 0) if name == 'C' else (piv, 0, 0), f)
        ok = (px >= 1) & (px < 319) & (py >= 2) & (py < 200)
        got = new[py[ok], px[ok]].astype(int)
        for half, off in (('left', 0), ('right', 128)):
            want = env[row[ok] & 127, (col[ok] & 127) + off].astype(int)
            res.append('%s/%s: median |diff| %.1f' % (name, half, np.median(np.abs(got - want))))
    print(os.path.basename(p), int(d['ret']), '; '.join(res))
