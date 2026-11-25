"""Compare the reference capture's records (VGA memory at each refresh) with the complete draws of a run_bump.py
dump: which records show a complete frame, which a frame being drawn (rows of the new one over the old one).
usage: python3 check_capture.py ../../part2.frames runs/suomi FIRST LAST"""
import struct
import sys

import numpy as np

sys.path.insert(0, '../..')
from sheet import open_frames, indexed  # noqa: E402

raw, n = open_frames(sys.argv[1])
d = open(sys.argv[2] + '.draws', 'rb').read()
draws = []
for k in range(len(d) // 64012):
    fr, cnt, t = struct.unpack_from('<IIf', d, k * 64012)
    draws.append((fr, np.frombuffer(d, np.uint8, 64000, k * 64012 + 12).reshape(200, 320)))
first, last = int(sys.argv[3]), min(int(sys.argv[4]), n - 1)
stats = {'complete': 0, 'partial': 0, 'other': 0}
cuts = []
j = 0
for f in range(first, last + 1):
    px, pal = indexed(raw, f)
    # draws finished before record f was written: their 'done' frame (st['frame'] at the time) <= f
    while j + 1 < len(draws) and draws[j + 1][0] <= f:
        j += 1
    cur = draws[j][1] if draws[j][0] <= f else None
    nxt = draws[j + 1][1] if j + 1 < len(draws) else None
    if cur is not None and np.array_equal(px, cur):
        stats['complete'] += 1
        continue
    if cur is not None and nxt is not None:
        # a frame being drawn: pixels in raster order up to some point from the next draw, the rest from this one
        a, b = px.ravel(), nxt.ravel()
        newp = np.nonzero(a != cur.ravel())[0]
        oldp = np.nonzero(a != b)[0]
        if len(oldp) == 0 or len(newp) == 0 or newp.max() < oldp.min():
            stats['partial'] += 1
            cut = oldp.min() if len(oldp) else 64000
            cuts.append(cut // 320)
            continue
    stats['other'] += 1
    if stats['other'] <= 5:
        print('record %d matches neither (draw done at %s)' % (f, draws[j][0]))
print(stats)
if cuts:
    print('partial records: the new frame reaches row', np.percentile(cuts, [0, 25, 50, 75, 100]).tolist())
