"""Find the constant ring-table value (row shift, column shift) and picture that render a capture record exactly
(for records where the ring table is the same for every ring, i.e. A = 0 and d14 = 0: set 2)."""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import frame
from sheet import open_frames, indexed
raw, _ = open_frames(os.path.join(frame.RE, 'full.frames'))
for k in map(int, sys.argv[1:]):
    px, _ = indexed(raw, k)
    # the pixel at one screen point gives the candidate ring-table values; check the whole frame for those
    cand = []
    for p in range(5):
        P = frame.pic(p)
        y0, x0 = 37, 51
        w1 = int(frame.W1[y0, x0])
        for t in np.nonzero(P[((np.arange(65536) + w1) & 0xffff) ^ 0x8000] == px[y0, x0])[0]:
            w = (int(t) + frame.W1) & 0xffff
            img = P[w ^ 0x8000].reshape(200, 320)
            d = int((img != px).sum())
            if d < 2000:
                cand.append((d, p, int(t) >> 8, (int(t) & 0xff)))
    cand.sort()
    print(k, cand[:5])
