"""Check the transform and projection conventions given in the notes against the engine's per-vertex results in a
dump: view = [x y z] . M(a, b, c) + P (row vector), M = Rx(a) Ry(b) Rz(c) (angles in 1/4096 turn), screen x =
160 + 1024 X / ((D - Z) 2^sh), y = 100 + 1024 Y / ((D - Z) 2^sh) (X, Y, Z in object units, P = position / 1024 for
objects drawn directly with 0x789c4, P = position for objects in a 0x79483 scene).

usage: transform_check.py DUMP OBJ [scene|direct] [da,db,dc [dx,dy,dz]] (angle / position steps per
tick, to undo the PIT ticks between the draw and the dump)
"""
import pickle
import sys

import numpy as np

import snap


def rot(a, b, c):
    a, b, c = (2 * np.pi * x / 4096 for x in (a, b, c))
    Rx = np.array([[1, 0, 0], [0, np.cos(a), -np.sin(a)], [0, np.sin(a), np.cos(a)]])
    Ry = np.array([[np.cos(b), 0, np.sin(b)], [0, 1, 0], [-np.sin(b), 0, np.cos(b)]])
    Rz = np.array([[np.cos(c), -np.sin(c), 0], [np.sin(c), np.cos(c), 0], [0, 0, 1]])
    return Rx @ Ry @ Rz


COS = np.frombuffer(snap.IMG, np.int16, 4096, 0x5599c).astype(np.int64)


def imat(a, b, c):
    """0x7984e in integers: COS[i] = trunc(1023 cos), sin = COS[i - 0x400], products >> 10 (shrd = floor)."""
    ca, sa = COS[a & 0xfff], COS[(a - 0x400) & 0xfff]
    cb, sb = COS[b & 0xfff], COS[(b - 0x400) & 0xfff]
    cc, sc = COS[c & 0xfff], COS[(c - 0x400) & 0xfff]
    m = np.zeros((3, 3), np.int64)
    m[0] = [(cc * cb) >> 10, (-sc * cb) >> 10, sb]
    m[1] = [(sc * ca >> 10) + ((((sb * cc) >> 10) * sa) >> 10), (cc * ca >> 10) - ((((sa * sb) >> 10) * sc) >> 10),
            (-sa * cb) >> 10]
    m[2] = [(sc * sa >> 10) - ((((sb * cc) >> 10) * ca) >> 10), (cc * sa >> 10) + ((((ca * sb) >> 10) * sc) >> 10),
            (ca * cb) >> 10]
    return m


if __name__ == '__main__':
    d = pickle.load(open(sys.argv[1], 'rb'))
    m = snap.Mem(d['mem'])
    obj = int(sys.argv[2], 16)
    scene = len(sys.argv) > 3 and sys.argv[3] == 'scene' 
    P = np.array([m.s32(obj), m.s32(obj + 4), m.s32(obj + 8)], float)
    if not scene: P = P / 1024
    ang = [m.s16(obj + 0xc), m.s16(obj + 0xe), m.s16(obj + 0x10)]
    base = m.u32(obj + 0x12)
    nv = m.u16(base)
    V = m.arr(base + 4, nv * 8, np.int16).reshape(nv, 8)[:, :3].astype(float)
    M = rot(*ang)
    view = V @ M + P
    D, sh = m.s32(0x57ab6), m.u8(0x5594c)
    den = (D - view[:, 2]) * 2 ** sh
    sx = 160 + 1024 * view[:, 0] / den
    sy = 100 + 1024 * view[:, 1] / den
    eb, sb = m.u32(0x558ea), m.u32(0x558e4)
    ev = np.array([[m.s32(eb + 16 * i + 4 * k) for k in range(3)] for i in range(nv)], float) / 1024
    es = np.array([[m.s16(sb + 4 * i), m.s16(sb + 4 * i + 2)] for i in range(nv)], float)
    ok = den > 0
    C = imat(0, 0, 0)
    # integer version: object matrix, times the camera matrix M(0,0,0) for scene objects (0x79d25), >> 10 each
    # the PIT callback may have stepped the angles between the transform and the dump: try earlier ticks
    step = [int(x, 0) for x in sys.argv[4].split(',')] if len(sys.argv) > 4 else [0, 0, 0]
    evi = (ev * 1024).round().astype(np.int64)
    for k in range(4):
        ak = [x - k * s_ for x, s_ in zip(ang, step)]
        Mi = imat(*ak)
        if scene: Mi = (Mi @ C) >> 10
        Vi = V.astype(np.int64)
        Pi = np.array([m.s32(obj), m.s32(obj + 4), m.s32(obj + 8)], np.int64) << (10 if scene else 0)
        if len(sys.argv) > 5: Pi = Pi - k * np.array([int(x, 0) for x in sys.argv[5].split(',')], np.int64)
        if scene: Pi = (Pi @ C) >> 10                        # 0x79d25 multiplies the translation row as well
        vi = Vi @ Mi + Pi
        den = ((D - (vi[:, 2] >> 10)) << sh)
        okk = den > 0
        sxi = np.where(okk, (np.trunc(vi[:, 0] / np.where(okk, den, 1))).astype(np.int64) + 160, 0)
        syi = np.where(okk, (np.trunc(vi[:, 1] / np.where(okk, den, 1))).astype(np.int64) + 100, 0)
        e1 = np.abs(vi - evi).max()
        e2 = max(np.abs(sxi - es[:, 0])[okk].max(), np.abs(syi - es[:, 1])[okk].max())
        # env coordinates (0x79a17 with M(object angles), then 0x4da28 / 0x4d4b4: >> 12, mode 2 0x4d4e7: >> 11)
        N = m.arr(base + 4, nv * 8, np.int16).reshape(nv, 8)[:, 3:6].astype(np.int64)
        nr = N @ imat(*ak)
        mode = m.u32(0x55948)
        shn = 11 if mode == 2 else 12
        eu, evv = (nr[:, 0] >> shn) + 128 & 255, (nr[:, 1] >> shn) + 128 & 255
        eb2 = m.u32(0x55908)
        if mode in (15, 20, 21):
            got = np.array([[m.u8(eb2 + 4 * i + 2), m.u8(eb2 + 4 * i + 3)] for i in range(nv)])
        else:
            got = np.array([[m.u8(eb2 + 2 * i), m.u8(eb2 + 2 * i + 1)] for i in range(nv)])
        e3 = np.abs(np.stack([eu, evv], 1) - got).max()
        if e1 == 0 or k == 3:
            print('  env coordinates (mode %d, normal >> %d): max error %d' % (mode, shn, e3))
            print('  integer model (angles %d ticks before the dump): view coordinates max error %d / 1024 units, '
                  'screen coordinates max error %d px' % (k, e1, e2))
            break
    print('%s: object %#x pos %s angles %s, D %d sh %d' % (sys.argv[1], obj, P.round(1).tolist(), ang, D, sh))
    print('  view coordinates: max |model - engine| = %.2f units (object size %.0f)' % (
        np.abs(view - ev).max(), np.abs(V).max()))
    print('  screen coordinates: max |model - engine| = %.2f px over %d vertices in front of the eye' % (
        np.abs(np.stack([sx, sy], 1) - es)[ok].max(), ok.sum()))
