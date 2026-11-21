"""Pixels whose final value depends on what was in the back buffer before the faces were drawn: run_temple.py
--sentinel 255 fills the buffer with 255 before every temple frame's faces; compare with full.frames."""
import sys, os
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
from sheet import open_frames, indexed
raw, n = open_frames(sys.argv[1]); f0 = int(sys.argv[2])
full, _ = open_frames(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'full.frames'))
tot = 0; frames = 0; worst = (0, 0)
hist = []
for k in range(n):
    a, _ = indexed(raw, k); b, _ = indexed(full, f0 + k)
    dd = int((a != b).sum())
    if dd:
        frames += 1; tot += dd; hist.append((f0 + k, dd))
        if dd > worst[1]: worst = (f0 + k, dd)
print('refreshes', n, 'with differences', frames, 'pixels total', tot, 'worst', worst)
print('first ones', hist[:30])
