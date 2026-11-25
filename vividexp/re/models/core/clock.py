"""Clock relations from a capture's .ticks file: frame counter vs refresh, music position vs refresh.

usage: python3 models/core/clock.py full.ticks
"""
import sys
from collections import Counter

rows = []
for line in open(sys.argv[1]):
    if line.startswith('#'):
        continue
    f = line.split()
    if len(f) < 9:
        continue
    rows.append((int(f[0]), float(f[1]), int(f[2]), int(f[3]), int(f[4]), int(f[5]), int(f[6]), int(f[7]), f[8]))

# 1. refresh index k vs frame counter
prev = None
for r in rows:
    d = r[0] - r[4]
    if d != prev:
        print('k=%d counter=%d k-counter=%d part=%d line=%d' % (r[0], r[4], d, r[5], r[6]))
        prev = d
# counter steps other than +1
for a, b in zip(rows, rows[1:]):
    if b[4] - a[4] != 1 and a[4] > 0:
        print('counter step %d at k=%d (part %d line %d)' % (b[4] - a[4], b[0], b[5], b[6]))

# 2. music position changes: refreshes per row
changes = []
for a, b in zip(rows, rows[1:]):
    if (b[2], b[3]) != (a[2], a[3]):
        changes.append((b[0], b[2], b[3], b[5], b[7]))
print('position changes:', len(changes))
gaps = Counter()
for a, b in zip(changes, changes[1:]):
    if a[3] == b[3]:
        gaps[(a[3], b[0] - a[0])] += 1
print('refreshes between row changes (part, gap): count', sorted(gaps.items()))
# per order: first refresh of row 0
for c in changes:
    if c[2] == 0:
        print('part %d order %3d row 0 first seen at record %6d (irqs %d)' % (c[3], c[1], c[0], c[4]))
