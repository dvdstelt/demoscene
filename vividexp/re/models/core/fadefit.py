"""For records FIRST..LAST of a capture, find the number of fade steps j that reproduces the DAC exactly, for a fade
from palette P1 to P2 in STEPS steps (0x4020b8 / 0x401ed0 arithmetic).

usage: python3 models/core/fadefit.py full.frames P1 P2 STEPS FIRST LAST
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
from core_model import lookup                  # noqa: E402
from sheet import open_frames, indexed         # noqa: E402

frames, n1, n2, steps, first, last = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
p1 = np.frombuffer(lookup(n1), np.uint8).reshape(256, 3).astype(np.int64)
p2 = np.frombuffer(lookup(n2), np.uint8).reshape(256, 3).astype(np.int64)
d = (p2 - p1) << 16
d = np.sign(d) * (np.abs(d) // steps)
raw, n = open_frames(frames)
prev = None
for k in range(first, min(last, n - 1) + 1):
    _, pal = indexed(raw, k)
    js = [j for j in range(steps + 1) if (((((p1 << 16) + j * d) >> 16) & 63) == pal).all()]
    print(k, js[:4] if len(js) < 5 else (js[0], '..', js[-1]), '' if prev is None or not js or not prev else js[0] - prev[0])
    prev = js
