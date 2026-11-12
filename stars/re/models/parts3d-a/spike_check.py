"""Check the spike ball part's model (callback 0x6905, phases 0x6791, path 0x6888, palette 0x6861) against the
emulator: run_hook.py --hook 0x6584 --hook-dump 55836:64,6498:32,6518:4,6789:8 (just before the frame is shown and
the scene rendered)."""
import sys

import numpy as np

from common import COS1023, file
from hooklib import parse, i32, i16

P15 = np.frombuffer(file(15), np.uint8).reshape(256, 3).astype(int)


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def path(a):                                         # 0x6888, a = [0x678d] (1/4096 turns)
    y = (int(COS1023[(2 * a) & 0xfff]) * 0xb4) >> 10
    z = ((int(COS1023[a & 0xfff]) * 0x17c) >> 10) - 0x32a
    x = (int(COS1023[(a - 0x400) & 0xfff]) * 0x46) >> 10
    return x, y, z


recs = parse(sys.argv[1])
bad = {'path': 0, 'ang': 0, 'env': 0, 'pal': 0}
ang_err = []
prev = None
for r in recs:
    n = i32(r['6518'], 0)
    ph, palflag, end, mflag = r['6789'][0:4]
    a = i16(r['6789'], 4) & 0xffff
    L = i16(r['6789'], 6)
    o = r['6498']
    pos = (i32(o, 0), i32(o, 4), i32(o, 8))
    if pos != path(a):
        bad['path'] += 1
        if bad['path'] < 4: print('path', r['frame'], n, a, pos, path(a))
    # angles: x += 2, y += 3 (+7 from phase 4), z += 4 per tick; env x += 0x21 (+0x14 from phase 4)
    extra = max(0, n - 0x45a)                   # ticks in phase >= 4 (approximately: the phase is set per frame)
    exp_ang = (s16(2 * n), s16(3 * n + 7 * extra), s16(4 * n))
    got_ang = (i16(o, 0xc), i16(o, 0xe), i16(o, 0x10))
    env = i16(r['55836'], 0x2c)
    exp_env = s16(0x21 * n + 0x14 * extra)
    ang_err.append((got_ang[1] - exp_ang[1]) & 0xffff)
    if got_ang[0] != exp_ang[0] or got_ang[2] != exp_ang[2]:
        bad['ang'] += 1
    pal = np.frombuffer(r['pal'], np.uint8).reshape(256, 3).astype(int)
    if palflag and not (pal == (P15 * L) >> 6).all():
        bad['pal'] += 1
    prev = r
print(len(recs), 'frames; mismatches', bad)
e = np.array(ang_err); e = np.where(e > 0x8000, e - 0x10000, e)
print('angle y offset from 3n + 7 (n - 0x45a): values', sorted(set(e.tolist()))[:10])
# motion angle a vs ticks: a = 4 * (ticks with the motion flag on)
for r in recs[::120]:
    n = i32(r['6518'], 0); a = i16(r['6789'], 4) & 0xffff
    print('frame', r['frame'], 'tick', n, 'phase', r['6789'][0], 'a', a, 'a/4 - (n - 0x270) =', a // 4 - (n - 0x270), 'flag', r['6789'][3], 'level', i16(r['6789'], 6))
print('last', recs[-1]['frame'], i32(recs[-1]['6518'], 0), 'phase', recs[-1]['6789'][0], 'level', i16(recs[-1]['6789'], 6))
