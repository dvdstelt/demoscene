"""Model of the temple's animated state (0x403f10 tick, 0x4040b0 per frame) and a check against run_temple.py dumps.

  n  = tick routine calls since the part started (holder angle / 0.01)
  T  = [0x4248c4] (timer callbacks, 280 Hz) as the frame routine read it
"""
import math, sys
import numpy as np
import dumpread as D

PI = math.pi
# the program's constants (doubles in INSIDE.EXE; the compiler turned x / 179.0 into x * (1 / 179.0))
DEG = 0.01745329252                 # 0x420ab4
PI2 = 1.5707963268                  # 0x420acc
PI_ = 3.1415926536                  # 0x400921fb544486e0 (pushed as an immediate)
A15 = 0.2617993878                  # 15 degrees, 0x3fd0c152382daf40


def Rx(t): c, s = math.cos(t), math.sin(t); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
def Ry(t): c, s = math.cos(t), math.sin(t); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
def Rz(t): c, s = math.cos(t), math.sin(t); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
def R(a, b, c): return Rx(a) @ Ry(b) @ Rz(c)          # 0x4145f4


def trunc(x): return int(math.trunc(x))


# flare base positions: rand() from seed 0x45eafc after 9 calls (randseq.py)
def flare_bases():
    seed = 0x45eafc
    def rnd():
        nonlocal seed
        seed = (seed * 0x41c64e6d + 0x3039) & 0xffffffff
        return (seed >> 16) & 0x7fff
    for _ in range(9): rnd()
    out = []
    for i in range(10):
        z = rnd() % 1000 - 500; y = rnd() % 300 + 70; x = rnd() % 1000 - 500
        out.append(np.array([x, y, z], float))
    return out


FLARES = flare_bases()


def matrices(n):
    """the accumulated matrices after n tick calls (closed forms; the original multiplies step by step)"""
    H = Ry(0.01 * n)                                   # dummy camera holder 0x425a68
    C = R(A15, 0, PI_) @ Rz(0.005 * n)                 # camera 0x425e10
    F = [np.linalg.matrix_power(R(-0.01 * (10 - i), -0.03 * i, 0), n) for i in range(10)]
    return H, C, F


def putki_matrices(T):
    """tick 0x403f10: objects 3 and 4 (the swinging pipes), from the T the tick saw"""
    A = math.sin(T * (1 / 179.0)) * 35 * DEG
    B = math.cos(T * (1 / 231.4)) * 33.3 * DEG
    return R(A + PI2, B, 0), R(PI2 - A, -B, 0)


def frame_state(T, n):
    """what 0x4040b0 hands the engine for a frame drawn with clock T after n ticks"""
    H, C, F = matrices(n)
    k = 0.6 + 0.3 * math.sin(T * (1 / 891.1))
    cam_pos = np.array([0, trunc(320 * k), trunc(-850 * k)], float)
    fl = []
    for i in range(10):
        s = abs(math.sin((T + 13 * i) * (1 / 271.1)) * 0.7) + 0.15
        fl.append(np.array([trunc(c * s) for c in FLARES[i]], float))
    light = trunc(abs(math.sin(T * (1 / 461.1))) * 1000)
    glow = []
    for j in range(6):                                  # vertices 5..10 of objects 3, 4, 5
        w = math.sin((T + 265 + 53 * j) / (55 + 11 * j + 971.1)) * 16 * 16 * 0.95
        glow.append((trunc(w + 256), trunc(w + 320), trunc(w + 352)))
    return dict(H=H, C=C, F=F, cam_pos=cam_pos, flares=fl, light=light, glow=glow)


def check(rec):
    m = D.Blocks(rec)
    hold, cam = m.d32(0x425ecc), m.d32(0x425e74)
    Hd = m.mat(hold + 0x28)
    th = math.atan2(Hd[0, 2], Hd[0, 0])
    # the angle wraps at 2 pi: take the tick count near the refresh number whose angle matches
    cands = range(rec['frame'] - 11280, rec['frame'] - 11270)
    n = min(cands, key=lambda c: abs(math.remainder(0.01 * c - th, 2 * PI)))
    T = rec['T0']
    s = frame_state(T, n)
    err = {}
    err['H'] = np.abs(Hd - s['H']).max()
    err['C'] = np.abs(m.mat(cam + 0x28) - s['C']).max()
    err['Cw'] = np.abs(m.mat(cam + 0x70) - s['H'] @ s['C']).max()
    err['campos'] = np.abs(np.array(m.ints(cam + 0x10, 3)) - s['cam_pos']).max()
    cw = np.array([trunc(x) for x in s['H'] @ s['cam_pos']])
    err['camw'] = np.abs(np.array(m.ints(cam + 0x1c, 3)) - cw).max()
    fe = 0; fm = 0
    for i in range(10):
        f = m.d32(0x425e78 + 4 * i)
        fe = max(fe, np.abs(np.array(m.ints(f + 0x10, 3)) - s['flares'][i]).max())
        fm = max(fm, np.abs(m.mat(f + 0x28) - s['F'][i]).max())
    err['flarepos'] = fe; err['flareM'] = fm
    err['light'] = abs(m.i32(m.d32(0x425e5c) + 0xc8) - s['light'])
    ge = 0
    for oi in (3, 4, 5):
        o = m.d32(0x425de0 + 4 * oi); v0 = m.d32(o + 0xd0)
        for j in range(6):
            ge = max(ge, abs(m.i16(v0 + 0x40 * (5 + j) + 0x30) - s['glow'][j][oi - 3]))
    err['glow'] = ge
    return int(n), T, err


if __name__ == '__main__':
    d = D.load(sys.argv[1])
    for rec in d:
        n, T, e = check(rec)
        print(rec['frame'], 'n', n, 'T', T, ' '.join('%s=%.3g' % kv for kv in e.items()))
