"""Compare a resumed run's .frames with full.frames (pixels and DAC), per record: python3 cmpcap.py run.frames first"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
from sheet import open_frames, indexed
full, nf = open_frames(os.path.join(HERE, '..', '..', 'full.frames'))
run, nr = open_frames(sys.argv[1])
f0 = int(sys.argv[2])
bad = 0
for k in range(nr):
    a, pa = indexed(run, k)
    b, pb = indexed(full, f0 + k)
    dp, dd = (a != b).sum(), (pa != pb).sum()
    if dp or dd:
        bad += 1
        print('frame %d: %d pixels, %d DAC bytes differ' % (f0 + k, dp, dd))
print('%d of %d records differ' % (bad, nr))
