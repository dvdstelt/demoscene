"""Frame rate of the capture in the particle parts (1, 3, 6), per order, from full.flips.

dt of frame n = t70[n] - t70[n-1] (the t70 column is the timer read after the previous frame, see NOTES.md).
usage: python3 fps_table.py [../../full.flips]
"""
import sys
import os
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'full.flips')
rows = []
for line in open(path):
    if line.startswith('#'): continue
    f = line.split()
    rows.append((int(f[0]), int(f[1]), float(f[2]), int(f[3]), float(f[4]), int(f[5]), int(f[6]), int(f[7])))
by = collections.OrderedDict()
prev = None
for r in rows:
    flip, tick, t, timer, t70, part, order, row = r
    if prev is not None and part in (1, 3, 6) and timer > -(1 << 39) and prev[3] > -(1 << 39):
        dt = t70 - prev[4]
        key = (part, order, row // 16)
        by.setdefault(key, []).append(dt)
    prev = r
print('part order rows    frames  mean_dt   fps(=70/mean dt)  min_dt  max_dt')
for (part, order, q), dts in by.items():
    m = sum(dts) / len(dts)
    print('%4d   %02x   %02x-%02x  %5d  %7.3f   %6.1f          %6.3f  %6.3f' % (
        part, order, q * 16, q * 16 + 15, len(dts), m, 70 / m if m > 0 else 0, min(dts), max(dts)))
