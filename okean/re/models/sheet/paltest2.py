"""Which fade level (b) and which formula each captured palette of the ending has"""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sheet_model import *
sys.path.insert(0, RE)
from sheet import open_frames
T = Tables()
raw, W, H, rec, hdr, nfr = open_frames(os.path.join(RE, 'full.frames'))
for k in list(range(8028, 8040)) + list(range(8042, 8060)) + list(range(8090, 8100)):
    o = hdr + k * rec
    cp = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8).astype(int)
    fade = [b for b in range(65) if ((T.pal1 * b) >> 6 == cp).all()]
    cross = [b for b in range(65) if ((T.pal1 * b + T.pal2 * (64 - b)) >> 6 == cp).all()]
    print(k, 'fade', fade, 'cross', cross)
