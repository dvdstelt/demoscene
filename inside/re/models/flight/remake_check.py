"""How far does a plain-double evaluation of the flight motion (as a C++ remake without 80-bit long double would
do it, with the integer rule for the angle arguments n1..n4) drift from the original? python3 remake_check.py"""
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fdump  # noqa: E402
import flightmodel as fm  # noqa: E402


def n_rule(T, i):
    """n1..n4 of 0x401dbc from the part tick counter T with integer arithmetic (matches the x87 for all T)"""
    def below(v100):            # v100 = 100 * value (an integer); the original's value is just below it (T > 0)
        if T > 0 and v100 > 0:
            return (v100 - 1) // 100
        return -((-v100) // 100) if v100 < 0 else v100 // 100
    n1 = (132 * T + 33660 * i) // 1000
    n2 = below(12 * T + 9100 - 1700 * i)
    n3 = below(12 * T - 4200 + 9100 * i)
    n4 = below(12 * T - 8100 - 1350 * i)
    return n1, n2, n3, n4


class DoubleFlight(fm.Flight):
    def ship(self, i, s, ticks):
        s = float(np.float32(s))
        self.p[i] = self.p[i] + self.m[i] @ np.array([0.0, 0.0, s])
        n1, n2, n3, n4 = n_rule(ticks, i)
        a = float(np.float32(math.sin(n1 * fm.HALF_PI * float(np.float32(i * 0.25 + 1.7)) * 0.0078125)))
        r = math.cos(n2 * fm.HALF_PI * float(np.float32(1.2)) / 128) * 0.020943951024 + a * 0.010471975512
        r = math.cos(n3 * fm.HALF_PI * float(np.float32(-1.71)) / 128) * -0.013962634016 + r
        r = r - math.sin(n4 * fm.HALF_PI * float(np.float32(2.31)) / 128) * -0.006981317008
        if abs(r) > 0.006981317008:
            c, sn = math.cos(r), math.sin(r)
            self.m[i] = self.m[i] @ np.array([[c, 0, sn], [0, 1, 0], [-sn, 0, c]])
        p = self.p[i]
        p[1] *= 0.99
        if i > 0:
            for k in (0, 2):
                if p[k] > self.p[0][k]:
                    p[k] -= s * 0.65
                if p[k] < self.p[0][k]:
                    p[k] += s * 0.65
        h = i * 0.3 + 1.7
        if h * -512.0 < p[1]:
            p[1] = -(h * 512.0)

    def camera(self, s):
        s = float(np.float32(s))
        self.c = self.c + self.cm @ np.array([0.0, 0.0, s])
        self.c[1] *= 0.995
        if self.c[1] > -1024.0:
            self.c[1] = -1024.0
        dv = self.q - self.c
        f = dv / math.sqrt(dv @ dv)
        a = np.array([-f[2], 0.0, f[0]]); a /= math.sqrt(a @ a)
        u = np.cross(f, a); u /= math.sqrt(u @ u)
        m = np.array([a, u, f])
        c, sn = math.cos(3.1415926536), math.sin(3.1415926536)
        m = m @ np.array([[c, -sn, 0], [sn, c, 0], [0, 0, 1]])
        c, sn = math.cos(-0.20943951024), math.sin(-0.20943951024)
        self.cm = m @ np.array([[1, 0, 0], [0, c, -sn], [0, sn, c]])

    def tick(self, ticks):
        self.ship(0, fm.SPEED * 0.4, ticks)
        self.ship(1, fm.SPEED * 0.3, ticks)
        self.ship(2, fm.SPEED * 0.2, ticks)
        self.q = self.p[0].copy()
        self.camera(fm.SPEED * 0.3)


if __name__ == '__main__':
    bad = sum(n_rule(T, i) != fm.ship_angle(i, T)[2] for T in range(0, 20000) for i in range(3))
    print('integer rule for n1..n4: %d mismatches for T = 0..19999' % bad)
    recs = fdump.read(os.path.join(HERE, 'run1.dump'))
    f = DoubleFlight()
    worst = wm = 0
    jumps, mind = [], 1e9
    for r in recs:
        if r.frame >= 8468:
            break
        if r.kind == b'TICK':
            worst = max(worst, max(abs(r.vec(0x424ec8 + 0x18 * i) - f.p[i]).max() for i in range(3)),
                        abs(r.vec(0x424e18) - f.c).max())
            wm = max(wm, abs(r.mat(0x424e30) - f.cm).max())
            f.tick(r.ticks)
            mind = min(mind, min(np.linalg.norm(f.p[i] - f.c) for i in range(3)))
        elif r.kind == b'REND' and f.jump():
            jumps.append(r.frame)
    print('plain doubles: max position error %.3g, camera matrix %.3g, jumps at %s' % (worst, wm, jumps))
