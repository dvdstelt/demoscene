"""For every drawn frame of p2 (F lines of out/runs/p2.p2log): faces whose 16-bit back-face cross product (engine 3377)
has the wrong sign, with their true screen area (no occlusion): which frames lose big front faces or show back
faces."""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mesh import mesh
from project import project


def s16(v): return ((v + 0x8000) & 0xffff) - 0x8000


M = {'discs': mesh(0x1e71), 'C': mesh(0x2121)}
piv = {}
rows = []
for line in open(os.path.join(HERE, 'out', 'runs', 'p2.p2log')):
    f = line.split()
    if f[0] == 'R':
        piv[int(f[1])] = int(f[15])
    elif f[0] == 'F':
        ret, frame = int(f[1]), int(f[2])
        kv = dict(t.split('=') for t in f[4:-12]); cam = list(map(int, f[-12:]))
        x, y, z, vx, vy, vz, a, b, c, va, vb, vc = cam
        lost = shown = 0
        for name, fsc in (('discs', 4000), ('C', 1300)):
            if name == 'C' and int(kv['Chidden']) != 0: continue
            m = M[name]; F = m['F'][:, 1:] // 4
            px, py, Z = project(m['V'], (a, b, c), (x, y, z), (piv.get(ret + 1, 0) if name == 'discs' else 0, 0, 0), fsc)
            X = [px[F[:, k]] for k in range(3)]; Y = [py[F[:, k]] for k in range(3)]
            true = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0])
            c16 = s16(s16(s16(X[1] - X[0]) * s16(Y[2] - Y[0])) - s16(s16(X[2] - X[0]) * s16(Y[1] - Y[0])))
            onscr = (np.stack(X).max(0) >= 0) & (np.stack(X).min(0) < 320) & (np.stack(Y).max(0) >= 1) & (np.stack(Y).min(0) <= 201)
            lost += int((np.abs(true[(true >= 0) & (c16 < 0) & onscr]) // 2).sum())
            shown += int((np.abs(true[(true < 0) & (c16 >= 0) & onscr]) // 2).sum())
        rows.append((ret, frame, lost, shown))
rows = np.array(rows)
bad = rows[(rows[:, 2] > 500) | (rows[:, 3] > 500)]
print('drawn frames', len(rows), '; frames losing > 500 px of front faces or showing > 500 px of back faces:', len(bad))
if len(bad):
    print('retrace ranges:', bad[0, 0], '..', bad[-1, 0], '; capture frames', bad[0, 1], '..', bad[-1, 1])
    hist = np.histogram(bad[:, 0], bins=[0, 3123, 4523, 5405, 6137, 6487, 7063, 7400])
    print('per phase (retrace bins):', list(zip(hist[1][:-1].tolist(), hist[0].tolist())))
    print('max lost px', rows[:, 2].max(), 'at retrace', rows[rows[:, 2].argmax(), 0])
