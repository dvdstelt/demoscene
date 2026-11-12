"""Check the float form of the transform (as a remake would evaluate it) against the engine's integer results:
R = Rz(-c) * Rx(-a) * Ry(b) with angles in 65536 units, view = R (v + pivot) + T/128, D = Tz/128 - view.z,
x = 160 - f/8 * view.x / D, y = 100 - 3f/32 * view.y / D (row in the 201-row target; screen row = y - 1),
env u = 64 + (R N).x / 512, env v = 64 + (R N).y / 512.
usage: python3 models/engine/float_check.py <part> <pre dump>"""
import math, os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpview import Dump
from engine_model import Engine, PARTS


def rot(a, b, c):
    t = lambda x: x * 2 * math.pi / 65536
    a, b, c = t(a), t(b), t(c)
    Ry = np.array([[math.cos(b), 0, math.sin(b)], [0, 1, 0], [-math.sin(b), 0, math.cos(b)]])
    Rx = np.array([[1, 0, 0], [0, math.cos(-a), -math.sin(-a)], [0, math.sin(-a), math.cos(-a)]])
    Rz = np.array([[math.cos(-c), -math.sin(-c), 0], [math.sin(-c), math.cos(-c), 0], [0, 0, 1]])
    return Rz @ Rx @ Ry


part, path = sys.argv[1], sys.argv[2]
d = Dump(path, part)
c = PARTS[part]
e = Engine(d, part)
e.build()
cam = d.b(0x10, c['objs'], 0x3a)
o = c['objs'] + 0x3a
vbase = 0
errs, eerr = [], []
while True:
    r = d.b(0x10, o, 0x3a)
    s = struct.unpack_from('<H', r, 0)[0]
    if s == 0:
        break
    if s != 0xffff and o <= c['objlast'] and struct.unpack_from('<H', r, 2)[0] == 0:
        seg = d.seg_of(s)
        nv, nf, opos, ofac, onrm, otex = (d.u16(seg, 2 * i) for i in range(6))
        if nf:
            f = struct.unpack_from('<h', r, 4)[0]
            ang = [(struct.unpack_from('<H', r, 0x1e + 2 * i)[0] + struct.unpack_from('<H', cam, 0x1e + 2 * i)[0]) & 0xffff for i in range(3)]
            T = [struct.unpack_from('<i', r, 6 + 4 * i)[0] + struct.unpack_from('<i', cam, 6 + 4 * i)[0] for i in range(3)]
            piv = struct.unpack_from('<3i', r, 0x2e)
            R = rot(*ang)
            for i in range(nv):
                v = np.array([d.s16(seg, opos + 6 * i + 2 * k) + piv[k] for k in range(3)], float)
                N = np.array([d.s16(seg, onrm + 6 * i + 2 * k) for k in range(3)], float)
                w = R @ v
                D = T[2] / 128 - w[2]
                x = 160 - f / 8 * (w[0] + T[0] / 128) / D
                y = 100 - 3 * f / 32 * (w[1] + T[1] / 128) / D
                g = 4 * (vbase + i)
                sx, sy = e.vscr[g]
                errs.append(max(abs(x - sx), abs(y - sy)))
                n = R @ N
                eu, ev = 64 + n[0] / 512, 64 + n[1] / 512
                if c['envneg']:
                    eu, ev = 128 - eu, 128 - ev
                ge = e.venv[g]
                eerr.append(max(abs(eu - (ge >> 16) / 256), abs(ev - (ge & 0xffff) / 256)))
            vbase += nv
    o += 0x3a
errs = np.array(errs)
print('%s frame %d: %d vertices; screen position |float - engine| max %.3f px, mean %.3f px; env coords max %.3f texels' % (
    part, d.frame, len(errs), errs.max(), errs.mean(), max(eerr)))
