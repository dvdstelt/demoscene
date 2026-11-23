"""What 0x4145f4(m, a, b, c) builds: compare with products of elementary rotations."""
import numpy as np
from emu import Emu, dpush
np.set_printoptions(precision=6, suppress=True, linewidth=150)
e = Emu()
buf = e.alloc(72)
def R(a, b, c):
    e.call(0x4145f4, eax=buf, stack=dpush(a, b, c)); return e.mat(buf)
def Rx(t): c, s = np.cos(t), np.sin(t); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
def Ry(t): c, s = np.cos(t), np.sin(t); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
def Rz(t): c, s = np.cos(t), np.sin(t); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
for (a, b, c) in [(0.3, 0, 0), (0, 0.3, 0), (0, 0, 0.3), (0.3, 0.5, 0.7)]:
    M = R(a, b, c)
    print((a, b, c)); print(M)
    for name, cand in [('Rx Ry Rz', Rx(a) @ Ry(b) @ Rz(c)), ('Rz Ry Rx', Rz(c) @ Ry(b) @ Rx(a)),
                       ('(Rx Ry Rz)^T', (Rx(a) @ Ry(b) @ Rz(c)).T), ('(Rz Ry Rx)^T', (Rz(c) @ Ry(b) @ Rx(a)).T)]:
        print('  ', name, np.abs(cand - M).max())
