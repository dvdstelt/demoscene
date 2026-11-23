"""The tunnel object (flower/tunneli1.sro): geometry survey and the per-vertex light level the part's start routine
0x4098c4 writes into vertex +0x30, checked against the snapshot (rand() replayed from the seed it had).

usage: python3 models/flower/tunnel.py [snaps/f4963.snap]
"""
import math
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sro  # noqa: E402

A, C = 0x41c64e6d, 0x3039
AINV = pow(A, -1, 1 << 32)


def light_levels(v, seed):
    """0x4099e4..0x409a8e. Returns (levels, seed after)."""
    out = []
    for i in range(len(v)):
        x, y = int(v['x'][i]), int(v['y'][i])
        val = math.sin(x * 0.05) * 300.0 * math.cos(y * 0.04) + math.sin(i * 0.0492610837) * 700.0
        val = int(val)                                   # trunc toward zero
        seed = (seed * A + C) & 0xffffffff
        r = (seed >> 16) & 0x7fff
        val += r % 600 - 300
        val = max(0, min(1023, val))
        out.append(val)
    return np.array(out), seed


def main():
    v, f = sro.load('flower/tunneli1.sro')
    print('tunnel: %d vertices, %d faces' % (len(v), len(f)))
    print('x %d..%d y %d..%d z %d..%d' % (v['x'].min(), v['x'].max(), v['y'].min(), v['y'].max(), v['z'].min(),
                                          v['z'].max()))
    print('u %d..%d v %d..%d, extra word values %s' % (v['u'].min(), v['u'].max(), v['v'].min(), v['v'].max(),
                                                       np.unique(v['w'])[:8]))
    print('face word 0 values', np.unique(f['m'])[:10])
    for i in range(0, 30):
        print(i, v[i])
    for j in range(0, 30):
        print('face', j, f[j])
    snap = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'snaps', 'f4963.snap')
    import unicorn  # noqa: F401
    s = pickle.load(open(snap, 'rb'))
    mem = s['mem']
    rd = lambda a: struct.unpack('<I', mem[a:a + 4])[0]
    seed_after = rd(0x423818)
    seed = seed_after
    for _ in range(len(v)):
        seed = ((seed - C) * AINV) & 0xffffffff
    print('seed in the snapshot %#010x, before the %d calls %#010x' % (seed_after, len(v), seed))
    lv, s2 = light_levels(v, seed)
    assert s2 == seed_after
    verts, faces = sro.obj_from_mem(mem, rd(0x4b661c))
    got = np.array([vv[0x30 // 4] & 0xffff for vv in verts])
    print('light levels: model equals snapshot for %d of %d vertices' % (np.count_nonzero(got == lv), len(lv)))
    xs = np.array([vv[2] for vv in verts]); print('snapshot x equals file x:', np.array_equal(xs, v['x']))
    flags = np.array([ff[0x30 // 4] for _, ff in faces])
    print('face flags bit0 (hidden): faces', np.flatnonzero(flags & 1)[:3], '..', 'visible', np.flatnonzero(~flags & 1)[[0, -1]],
          'count visible', np.count_nonzero(~flags & 1))
    print('all face flags', np.unique(flags))
    return v, f, lv


if __name__ == '__main__':
    main()
