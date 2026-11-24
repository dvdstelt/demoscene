"""compare every title record 5..620 with the capture (record k = draw k - 5 with k - 5 callbacks), reading the
bytes outside the mask from the snapshot (title_offscreen.py lists where that matters)"""
import sys, pickle
from title import *
seeds = seed_list(620)
s = snap('2c26f')
rd = snap_mask_reader(s)
bad = []
nvis = 0
for k in range(5, 621):
    cidx, pal = cap_frame(k)
    c = k - 5
    idx, _, out = frame(c, seeds[c], rd)
    m = (idx == cidx).mean()
    if m < 1:
        print(k, 'match %.6f' % m, 'ndiff', int((idx != cidx).sum()), flush=True)
        bad.append(k)
print('records not bit-exact:', len(bad), bad[:50])
