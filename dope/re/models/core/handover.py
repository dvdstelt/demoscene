"""Hand-overs between parts: DAC and screen statistics of the frames around each part boundary in full.frames,
and the exit fades of p6 / p7 checked against the code (3d22 in p6: level 256 - 7 n; 33be in p7: 256 - n;
DAC = (v * level) >> 8 per colour, one step per retrace start).

usage: python3 models/core/handover.py        (from dope/re, inside the toolbox)"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
import sheet

raw, W, H, rec, hdr, n = sheet.open_frames(os.path.join(RE, 'full.frames'))
parts = {}
for line in open(os.path.join(RE, 'full.ticks')):
    if line.startswith('#'):
        continue
    f = line.split()
    parts[int(f[0])] = (f[6], f[10], int(f[9]))


def rec_of(i):
    o = hdr + i * rec
    w, h, flags, _ = np.frombuffer(bytes(raw[o:o + 8]), '<u2')
    pal = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8).reshape(256, 3)
    idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:max(h, 1), :max(w, 1)]
    return w, h, flags, pal, idx


def desc(i):
    w, h, flags, pal, idx = rec_of(i)
    if not flags & 1:
        return 'f%d %s text' % (i, parts.get(i, ('?',))[0])
    used = np.unique(idx)
    rgb = pal[idx].astype(np.int32)
    return 'f%d %-3s %s keys=%d  mean rgb %5.1f %5.1f %5.1f  max %2d  colours used %3d  dac[0]=%s dac max %d' % (
        i, parts[i][0], parts[i][1], parts[i][2], rgb[..., 0].mean(), rgb[..., 1].mean(), rgb[..., 2].mean(),
        rgb.max(), len(used), tuple(pal[0]), pal.max())


if __name__ == '__main__':
    bounds = [b for b in sorted(parts) if b - 1 in parts and parts[b][0] != parts[b - 1][0]]
    for b in bounds:
        for i in range(b - 3, b + 9):
            if 0 <= i < n:
                print(desc(i))
        print()
    # exit fades
    for name, first, last, step in (('p6', 29540, 29596, 7), ('p7', 31800, 32119, 1)):
        print('exit fade', name)
        prev = None
        for i in range(first, last + 1):
            w, h, flags, pal, idx = rec_of(i)
            s = int(pal.astype(np.int32).sum())
            if s != prev:
                print('  f%d dac sum %d' % (i, s))
            prev = s
