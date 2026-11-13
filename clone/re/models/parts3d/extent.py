"""Which rows / columns the 3D objects reach in full.frames (non-black pixels outside the tile band)."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import numpy as np
from sheet import open_frames, to_rgb
raw, w, h, rec, hdr, n, _ = open_frames(os.path.join(os.path.dirname(__file__), '..', '..', 'full.frames'))
for part, t0, t1 in [(8, 3084, 3924), (9, 4621, 6155), (10, 10380, 11147), (7, 6924, 7883)]:
    rows = np.zeros(200, int); cols = np.zeros(320, int)
    for t in range(t0, t1 + 1, 3):
        f = to_rgb(raw, t, w, h, rec, hdr).astype(int).sum(2) > 0
        rows += f.any(1); cols += f.any(0)
    r = np.nonzero(rows)[0]; c = np.nonzero(cols)[0]
    print('part %2d: non-black rows %d..%d, columns %d..%d; frames touching row 4: %d, row 195: %d, row 0: %d, row 199: %d, col 0: %d, col 319: %d'
          % (part, r.min(), r.max(), c.min(), c.max(), rows[4], rows[195], rows[0], rows[199], cols[0], cols[319]))
