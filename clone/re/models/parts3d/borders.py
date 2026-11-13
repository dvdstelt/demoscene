"""Rows that a part does not clear: max / mean of rows 0..3 and 196..199 (and the band edges) in full.frames."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import numpy as np
from sheet import open_frames, to_rgb, read_ticks
raw, w, h, rec, hdr, n, _ = open_frames(os.path.join(os.path.dirname(__file__), '..', '..', 'full.frames'))
ticks = read_ticks(os.path.join(os.path.dirname(__file__), '..', '..', 'full.ticks'))
for part, t0, t1 in [(2, 2316, 3083), (8, 3084, 3924), (6, 3925, 4620), (9, 4621, 6155), (4, 9324, 10379), (10, 10380, 11147), (7, 6924, 7883)]:
    top = []; bot = []
    for t in range(t0, t1 + 1, 7):
        f = to_rgb(raw, t, w, h, rec, hdr).astype(int)
        top.append((f[0:4].max(), f[0:4].mean())); bot.append((f[196:200].max(), f[196:200].mean()))
    top = np.array(top); bot = np.array(bot)
    nz_t = (top[:, 0] > 0).sum(); nz_b = (bot[:, 0] > 0).sum()
    print('part %2d ticks %5d..%5d: rows 0..3 max %3d, non-black in %3d of %3d sampled ticks; rows 196..199 max %3d, non-black in %3d'
          % (part, t0, t1, top[:, 0].max(), nz_t, len(top), bot[:, 0].max(), nz_b))
