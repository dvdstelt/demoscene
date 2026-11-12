"""Prints, for a frame range of full.frames: DAC sum, DAC sum over the colours on screen, DAC[0], number of colours
on screen and their index range.
usage: python3 models/core/dacsum.py F1 F2     (from dope/re, inside the toolbox)"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
import sheet
raw, W, H, rec, hdr, n = sheet.open_frames(os.path.join(RE, 'full.frames'))
for i in range(int(sys.argv[1]), int(sys.argv[2]) + 1):
    o = hdr + i * rec
    pal = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8).reshape(256, 3)
    idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320]
    u = np.unique(idx)
    print(i, int(pal.astype(int).sum()), int(pal[u].astype(int).sum()), tuple(int(x) for x in pal[0]), len(u),
          u.min(), u.max())
