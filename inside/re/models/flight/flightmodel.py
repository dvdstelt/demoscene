"""Model of the flight part's motion (0x402948 init, tick 0x4021f8 with 0x401dbc / 0x4020f8, the camera jump
0x402638), checked against the TICK / REND records of run1.dump (python3 flightmodel.py).

Precision: the tick routine runs inside the timer interrupt, whose handler (0x40becf) does fnsave, which resets the
x87 to its power-on control word 0x037f: 64-bit (extended) precision. So the tick is modelled in np.longdouble
(x87 extended on x86-64 Linux) with double stores. The main line (init, frame routine) runs with Watcom's control
word 0x127f: 53-bit precision, i.e. plain doubles. fsin / fcos are computed by the emulator in double precision
on the host: sin(double(x)).
"""
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import rep  # noqa: E402

L = np.longdouble
f32 = np.float32
HALF_PI = rep.rdd(0x420662)          # 1.5707963268 (as written in the source, not pi/2)
SPEED = rep.rdd(0x4227ec)            # 17.066666666666666 (1024 / 60), double at 0x4227ec
K = {a: rep.rdd(a) for a in (0x42063a, 0x420642, 0x420652, 0x42065a, 0x42064a, 0x42066e, 0x420676, 0x42067e,
                             0x420686, 0x42068e, 0x420696, 0x42069e, 0x4206a6, 0x4206ae, 0x4206b6, 0x4206be,
                             0x4206c6, 0x4206de, 0x4206ee, 0x4206f6, 0x4206fe)}
PI_ROLL = 3.1415926536               # double 0x400921fb544486e0 pushed at 0x402194
PITCH = -0.20943951024               # double 0xbfcacee9f37c4b99 pushed at 0x4021a8 (-12 degrees)


def xsin(x): return L(math.sin(float(x)))     # emulator: host double sin
def xcos(x): return L(math.cos(float(x)))
def d(x): return np.float64(x)                # store to a qword


class Rand:
    """Watcom rand() 0x4134cd"""
    def __init__(self, seed): self.s = seed & 0xffffffff

    def __call__(self):
        self.s = (self.s * 0x41c64e6d + 0x3039) & 0xffffffff
        return (self.s >> 16) & 0x7fff


def rot(m, a, i, j, sign):
    """m = m . R: columns i, j: m[:, i] = m[:, i] c + sign * m[:, j] s, m[:, j] = m[:, j] c - sign * m[:, i] s"""
    c, s = L(d(xcos(a))), L(d(xsin(a)))
    out = m.copy()
    for r in range(3):
        a_, b_ = L(m[r, i]), L(m[r, j])
        out[r, i] = d(a_ * c + sign * (b_ * s))
        out[r, j] = d(b_ * c - sign * (a_ * s))
    return out


def rot_x(m, a): return rot(m, a, 1, 2, 1)    # 0x414270: m . Rx(a), Rx = [[1,0,0],[0,c,-s],[0,s,c]]
def rot_y(m, a): return rot(m, a, 0, 2, -1)   # 0x414344: m . Ry(a), Ry = [[c,0,s],[0,1,0],[-s,0,c]]
def rot_z(m, a): return rot(m, a, 0, 1, 1)    # 0x414418: m . Rz(a), Rz = [[c,-s,0],[s,c,0],[0,0,1]]


def norm(v):
    x, y, z = (L(t) for t in v)
    return np.sqrt((y * y + x * x) + z * z)   # 0x4136fc


def look(dv):
    """0x414cec: rows a, u, f with f = dir / |dir|, a = (-f.z, 0, f.x) / |..|, u = f x a / |..|"""
    inv = L(1) / norm(dv)
    f = np.array([d(L(t) * inv) for t in dv])
    a = np.array([-f[2], 0.0, f[0]])
    inv = L(1) / norm(a)
    a = np.array([d(L(t) * inv) for t in a])
    u = np.array([d(L(f[1]) * L(a[2]) - L(f[2]) * L(a[1])), d(L(f[2]) * L(a[0]) - L(f[0]) * L(a[2])),
                  d(L(f[0]) * L(a[1]) - L(f[1]) * L(a[0]))])
    inv = L(1) / norm(u)
    u = np.array([d(L(t) * inv) for t in u])
    return np.array([a, u, f])


def forward(p, m, s):
    """0x413d58 with dir (0, 0, s): p += M (0, 0, s)"""
    return np.array([d(L(p[k]) + L(d(L(m[k, 2]) * L(s)))) for k in range(3)])


def helper(n, f, fn):   # 0x4012e0 / 0x401308: fn(n * pi/2 * f / 128), extended, f a float
    return fn(L(n) * L(HALF_PI) * L(float(f32(f))) * L(0.0078125))


def trunc(x): return int(x)          # 0x40ba78 + fistp: toward zero


def ship_angle(i, ticks):
    """yaw of ship i in this tick (radians), and whether it is applied"""
    t = L(ticks) * L(K[0x42063a])                                   # T * 0.12
    n1 = trunc((t + L(i) * L(K[0x420642])) * L(K[0x42064a]))       # (T*0.12 + i*30.6) * 1.1
    f1 = f32(L(i) * L(K[0x420652]) + L(K[0x42065a]))                # (float)(i*0.25 + 1.7)
    a = f32(helper(n1, f1, xsin))                                   # stored as a float
    n2 = trunc((t + L(K[0x42066e])) - L(17 * i))                    # T*0.12 + 91 - 17i
    r = d(helper(n2, 1.2, xcos) * L(K[0x420676]) + L(a) * L(K[0x42067e]))
    n3 = trunc((t + L(K[0x420686])) + L(91 * i))                    # T*0.12 - 42 + 91i
    r = d(helper(n3, -1.71, xcos) * L(K[0x42068e]) + L(r))
    n4 = trunc((t + L(K[0x420696])) - L(i) * L(K[0x42069e]))       # T*0.12 - 81 - 13.5i
    rx = L(r) - helper(n4, 2.31, xsin) * L(K[0x4206a6])
    return d(rx), abs(rx) > L(K[0x4206ae]), (n1, n2, n3, n4)


class Flight:
    def __init__(self, part_seed=0x95a8f93f):
        r = Rand(0x45eafc)                       # srand at 0x402967
        x0 = r() % 0x3c00                        # 0x4018d0
        z0 = r() % 0x3c00
        self.p = [np.array([x0 + r() % 0x1400, 0x0a00 * i, z0 - 0x0a00 * i], float) for i in range(3)]
        self.m = [np.eye(3) for _ in range(3)]
        a, b = r() % 0x800, r() % 0xa00          # camera: ship 0 + (b, 0, a)
        self.c = self.p[0] + np.array([b, 0.0, a])
        self.cm = np.eye(3)
        a, b = r() % 0x800, r() % 0xa00          # control point: ship 0 + (b, 0, a)
        self.q = self.p[0] + np.array([b, 0.0, a])
        self.rand = Rand(part_seed)              # seed left by the earlier parts (capture: 0x95a8f93f)

    def ship(self, i, s, ticks):                 # 0x401dbc
        s = float(f32(s))
        self.p[i] = forward(self.p[i], self.m[i], s)
        r, apply, _ = ship_angle(i, ticks)
        if apply:
            self.m[i] = rot_y(self.m[i], r)
        p = self.p[i]
        p[1] = d(L(p[1]) * L(K[0x4206b6]))      # y *= 0.99
        if i > 0:
            p0 = self.p[0]
            step = L(s) * L(K[0x4206be])          # s * 0.65
            for k in (0, 2):
                if p[k] > p0[k]:
                    p[k] = d(L(p[k]) - step)
                if p[k] < p0[k]:
                    p[k] = d(L(p[k]) + step)
        h = L(i) * L(K[0x4206c6]) + L(K[0x42065a])   # i*0.3 + 1.7
        if h * L(-512.0) < L(p[1]):
            p[1] = d(-(h * L(512.0)))

    def camera(self, s):                         # 0x4020f8
        s = float(f32(s))
        self.c = forward(self.c, self.cm, s)
        self.c[1] = d(L(self.c[1]) * L(K[0x4206de]))    # y *= 0.995
        if self.c[1] > -1024.0:
            self.c[1] = -1024.0
        self.cm = look(np.array([d(L(self.q[k]) - L(self.c[k])) for k in range(3)]))
        self.cm = rot_z(self.cm, PI_ROLL)
        self.cm = rot_x(self.cm, PITCH)

    def tick(self, ticks):                       # 0x4021f8, once per refresh; ticks = [0x4248c8]
        sp = L(SPEED)
        self.ship(0, f32(sp * L(K[0x4206ee])), ticks)
        self.ship(1, f32(sp * L(K[0x4206f6])), ticks)
        self.ship(2, f32(sp * L(K[0x4206fe])), ticks)
        self.q = self.p[0].copy()
        self.camera(f32(sp * L(K[0x4206f6])))

    def jump(self):                              # 0x402638, per drawn frame (53-bit precision: doubles)
        hit = False
        for i in range(3):
            dv = self.p[i] - self.c
            if math.sqrt((dv[0] * dv[0] + dv[1] * dv[1]) + dv[2] * dv[2]) < 153.6:
                n1 = trunc((self.rand() % 10 + 5) * dv[0])
                n2 = trunc((self.rand() % 5 + 2) * dv[1])
                n3 = float(math.trunc((self.rand() % 10 + 5) * dv[2]))
                self.c = np.array([self.c[0] - n1, self.c[1] - n2, self.c[2] - n3])
                hit = True
        return hit


if __name__ == '__main__':
    import fdump
    recs = fdump.read(os.path.join(HERE, 'run1.dump'))
    f = Flight()
    worst = np.zeros(4)
    jumps = []
    nt = 0
    for r in recs:
        if r.frame >= 8468:
            break
        if r.kind == b'TICK':
            got = [r.vec(0x424ec8 + 0x18 * i) for i in range(3)] + [r.vec(0x424e18)]
            want = f.p + [f.c]
            e = max(abs(g - w).max() for g, w in zip(got, want))
            em = max(abs(r.mat(0x424a68 + 0x48 * i) - f.m[i]).max() for i in range(3))
            ec = abs(r.mat(0x424e30) - f.cm).max()
            eq = abs(r.vec(0x424fe0) - f.q).max()
            worst = np.maximum(worst, [e, em, ec, eq])
            f.tick(r.ticks)
            nt += 1
        elif r.kind == b'REND':
            if f.jump():
                jumps.append(r.frame)
    print('max abs error over %d ticks: positions %.3g, ship matrices %.3g, camera matrix %.3g, control %.3g'
          % (nt, *worst))
    print('camera jumps at refreshes', jumps)
