"""Painter's order of the flower against the tunnel: per dumped frame, the engine's sort keys of the visible flower
faces (bias +0xcc = 0x35200) and of the visible tunnel faces; counts tunnel faces that sort after (in front of) the
farthest flower face.

usage: python3 models/flower/order_check.py models/flower/run/part.dump
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dump  # noqa: E402


def keys(objb, V, F, cc):
    base = struct.unpack_from('<I', objb, 0xd0)[0]
    out = []
    for f in F:
        fl = int(f[12]) & 0xffffffff
        if not fl & 2 or fl & 1:
            continue
        zs = [int(V[((int(f[5 + j]) & 0xffffffff) - base) // 64][7]) for j in range(3)]
        out.append(max(0, int((max(zs) - cc) / 16)))
    return np.array(out)


frames = after = 0
for tag, frame, pl in dump.records(sys.argv[1]):
    if tag != 'DRWN':
        continue
    o = 0
    tun = pl[o:o + 0x110]; o += 0x110
    V = dump.verts(pl[o:], 732); o += 64 * 732
    F = dump.faces(pl[o:], 1440); o += 52 * 1440
    flb = pl[o:o + 0x110]; o += 0x110
    FV = dump.verts(pl[o:], 111); o += 64 * 111
    FF = dump.faces(pl[o:], 125)
    if struct.unpack_from('<I', flb, 0xc4)[0] & 1:
        continue
    kt, kf = keys(tun, V, F, 0), keys(flb, FV, FF, 0x35200)
    if not len(kf) or not len(kt):
        continue
    frames += 1
    n = np.count_nonzero(kt < kf.max())
    after += n > 0
    if n:
        print('refresh %d: flower keys %d..%d, %d tunnel faces with a smaller key (nearest tunnel key %d)' % (
            frame, kf.min(), kf.max(), n, kt.min()))
print('%d frames with the flower; %d with tunnel faces sorted in front of the flower\'s farthest face' % (frames, after))
