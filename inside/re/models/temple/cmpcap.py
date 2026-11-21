"""Compare a resumed run's .frames (starting at refresh F0) with full.frames: number of differing pixels per refresh."""
import sys, os
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
from sheet import open_frames, indexed
raw, n = open_frames(sys.argv[1]); f0 = int(sys.argv[2])
full, _ = open_frames(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'full.frames'))
bad = 0
for k in range(n):
    a, pa = indexed(raw, k); b, pb = indexed(full, f0 + k)
    d = int((a != b).sum()); dp = int((pa != pb).sum())
    if d or dp: bad += 1; print(f0 + k, 'pixels differ', d, 'palette entries differ', dp)
print('compared', n, 'refreshes,', bad, 'differ')
