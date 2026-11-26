import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sheet_model import *
sys.path.insert(0, RE)
from sheet import open_frames
T = Tables()
raw, W, H, rec, hdr, nfr = open_frames(os.path.join(RE, 'full.frames'))
for k in range(6892, 6900):
    o = hdr + k * rec
    cp = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8).astype(int)
    best = [b for b in range(65) if ((T.pal1 * b) >> 6 == cp).all()]
    print(k, 'b', best, cp[3:12])
