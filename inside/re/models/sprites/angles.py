"""Rotation of the warp grid and the box per frame, from dumps of models/sprites/run_inside.py.

The tick routine 0x404738 multiplies both objects' matrices by the step matrix [0x425ed8] = Ry(+-0.01) every
refresh: M = M0 Ry(0.01 m). This prints m (the accumulated angle / 0.01) for each dumped frame, with
M0 = Rx(1.5) Ry(3.0) for the warp grid (set at init) and the identity for the box (reset by 0x405ba0 / 0x40585c).
usage: python3 models/sprites/angles.py runs/a.dump.pkl [...]
"""
import pickle, struct, sys
import numpy as np


def Rx(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def Ry(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


M0 = Rx(1.5) @ Ry(3.0)
for path in sys.argv[1:]:
    frames = pickle.load(open(path, 'rb'))
    rows = []
    for fr in frames:
        W = np.array(struct.unpack_from('<9d', fr['warpgrid']['raw'], 0x28)).reshape(3, 3)
        K = np.array(struct.unpack_from('<9d', fr['kuubio']['raw'], 0x28)).reshape(3, 3)
        Wl = M0.T @ W                        # = Ry(angle)
        aw = np.arctan2(Wl[0, 2], Wl[0, 0])
        ak = np.arctan2(K[0, 2], K[0, 0])
        rows.append((fr['frame'], fr['pic'], aw / 0.01, ak / 0.01, np.abs(Wl - Ry(aw)).max()))
    for r in rows[:3] + rows[-3:]:
        print('%s frame %d pic %d warp grid m %.3f (mod 2pi/0.01 = 628.3), box m %.3f, residual %.1e' % (path, *r))
