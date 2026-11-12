"""Check the bee part's motion model (0x121b4 camera, 0x122d7 wings, 0x11fa1 palette) against the emulator.

Input: the dump of run_hook.py with --hook 0x11f23 (just before the scene is rendered) and
--hook-dump 55836:64,11b7a:4,121ac:8,11b86:8,11f9a:8,2b6d88:2176,127e8:4 (0x2b6d88 = the WLDD descriptors of
file 28 in this run).
"""
import struct
import sys

import numpy as np

from common import COS1023, IMG, file
from hooklib import parse, i32, i16

KEYS = [struct.unpack_from('<6h', IMG, 0x11d12 + 12 * k) for k in range(9)]
POSE = [[struct.unpack_from('<6h', IMG, 0x11bbe + 0x54 * p + 12 * o) for o in range(7)] for p in range(4)]
WING_OBJ = [struct.unpack_from('<I', IMG, 0x11ba2 + 4 * i)[0] for i in range(7)]
P26 = np.frombuffer(file(26), np.uint8).reshape(256, 3).astype(int).copy()
P26[0] = np.frombuffer(file(31), np.uint8)[:3]


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def lerp_div(a, b, t, d):                 # (b - a) * t / d with idiv (truncation), then 16-bit add
    q = int((s16(b - a) * t) / d)
    return s16(q + a)


def camera(key, sub, n):
    a, b = KEYS[key], KEYS[key + 1 if key < 8 else 0]
    pos = [lerp_div(a[i], b[i], sub, 0x8c) for i in range(3)]
    ang = [lerp_div(a[3 + i], b[3 + i], sub, 0x8c) for i in range(3)]
    s = int(COS1023[((n << 6) - 0x400) & 0xfff])
    env_x = s16(100 - (s >> 3))
    env_y = s16(400 + (s >> 4) + ang[1] - 0x122)
    pos[1] += s >> 6
    return pos, ang, env_x, env_y


def wings(wkey, wsub):
    out = []
    for o in range(7):
        a, b = POSE[wkey][o], POSE[(wkey + 1) & 3][o]
        out.append(tuple(s16(((s16(b[i] - a[i]) * wsub) >> 3) + a[i]) for i in range(6)))
    return out


def palette(n):
    if n < 0x10:
        L = min(n * 4, 0x3f)
        return ((63 + (((P26 - 63) * L) & 0xffff) // 64) & 0xff) & 0x3f    # 0x120df (logical shift of a 16-bit product)
    if n < 0x38e:
        return P26
    L = 0x3f + ((0x3ae - n) >> 1)
    L = max(0, min(0x3f, L))
    return (P26 * L) >> 6


recs = parse(sys.argv[1])
bad = {'cam': 0, 'env': 0, 'wing': 0, 'pal': 0}
offs = set()
for r in recs:
    n = i32(r['11b7a'], 0)
    key, sub = i32(r['121ac'], 0), i32(r['121ac'], 4)
    wsub, wkey = i32(r['11b86'], 0), i32(r['11b86'], 4)
    offs.add(key * 0x8c + sub - n)
    c = r['55836']
    gpos = [i32(c, 0), i32(c, 4), i32(c, 8)]
    gang = [i16(c, 0xc), i16(c, 0xe), i16(c, 0x10)]
    genv = (i16(c, 0x2c), i16(c, 0x2e))
    pos, ang, ex, ey = camera(key, sub, n)
    if gpos != pos or gang != ang:
        bad['cam'] += 1
        if bad['cam'] < 4: print('cam', r['frame'], n, gpos, pos, gang, ang)
    if genv != (ex, ey):
        bad['env'] += 1
        if bad['env'] < 4: print('env', r['frame'], genv, (ex, ey))
    d = r['2b6d88']
    w = wings(wkey, wsub)
    for o, idx in enumerate(WING_OBJ):
        q = 32 * idx
        got = (i32(d, q), i32(d, q + 4), i32(d, q + 8), i16(d, q + 0xc), i16(d, q + 0xe), i16(d, q + 0x10))
        if got != w[o]:
            bad['wing'] += 1
            if bad['wing'] < 4: print('wing', r['frame'], o, got, w[o])
    pal = np.frombuffer(r['pal'], np.uint8).reshape(256, 3).astype(int)
    if r['frame'] > 5949 and not (pal == palette(n)).all():
        # the palette is set after this frame's 0x11fa1 only if [0x11f9a]; compare with the state of this frame
        bad['pal'] += 1
        if bad['pal'] < 6: print('pal', r['frame'], n, np.abs(pal - palette(n)).max())
print(len(recs), 'frames; mismatches', bad, '; key*140+sub - n =', sorted(offs))
print('first', recs[0]['frame'], i32(recs[0]['11b7a'], 0), 'last', recs[-1]['frame'], i32(recs[-1]['11b7a'], 0))
