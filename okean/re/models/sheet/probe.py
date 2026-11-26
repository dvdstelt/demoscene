"""probe.py REC: which (seed advance, callback count) reproduces record REC best"""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sheet_model import *
sys.path.insert(0, RE)
from sheet import open_frames
T = Tables()
raw, W, H, rec, hdr, nfr = open_frames(os.path.join(RE, 'full.frames'))
def cap(k):
    o = hdr + k * rec
    return np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320].reshape(-1)
k = int(sys.argv[1]); d0 = int(sys.argv[2]); c0 = int(sys.argv[3])
seed = T.seed0
for i in range(d0 - 3):
    buf = np.empty(64000, np.uint8)
    seed = noise(buf, seed)
ci = cap(k)
for dd in range(d0 - 3, d0 + 4):
    buf = np.empty(64000, np.uint8)
    s2 = noise(buf, seed)
    nz = int(((buf != ci) & (ci < 32)).sum())
    print('noise of draw', dd, 'differs at', int((buf != ci).sum()), 'pixels;', nz, 'where capture < 32')
    seed = s2
seed = T.seed0
bufs = {}
for i in range(d0 + 3):
    buf = np.empty(64000, np.uint8)
    seed = noise(buf, seed)
    if i >= d0 - 2:
        bufs[i] = buf
for kk in range(k - 2, k + 2):
    ci = cap(kk).reshape(200, 320)
    line = []
    for i, b in bufs.items():
        m = ((b.reshape(200, 320) == ci).mean(1) > 0.5)
        rows = np.nonzero(m)[0]
        line.append('%d: %s' % (i, (rows.min(), rows.max(), len(rows)) if len(rows) else '-'))
    print('rec', kk, ' '.join(line))
