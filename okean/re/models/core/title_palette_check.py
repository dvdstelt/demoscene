"""Check of the palette-timing rule on the title part: its fade is computed after the retrace wait, so record k's
DAC uses the fade after n + 1 callbacks (n = the image's count). [0x15020] starts at its file value and loses 8 per
callback while the row word is >= 0x38; DAC = clamp((src[i] * f) >> 8, 0, 63), src = 0x2b65c."""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)
from sheet import open_frames
IMG = open(os.path.join(RE, 'unpacked/korso2.bin'), 'rb').read()
B = 0xec0
f0 = int.from_bytes(IMG[B + 0x15020:B + 0x15024], 'little')
src = np.frombuffer(IMG[B + 0x2b65c:B + 0x2b65c + 768], np.uint8).astype(np.int64)
pos = []
for line in open(os.path.join(HERE, 'runs', 'p1.ev')):
    x = line.split()
    if x[0] == 'cb' and x[4] == '2c26f':
        pos.append((int(x[6]), int(x[7])))
fade = [f0]
for o, r in pos:
    f = fade[-1]
    if r >= 0x38 and f != 0:
        f -= 8
    fade.append(f)
raw, W, H, rec, hdr, n = open_frames(os.path.join(RE, 'full.frames'))
F = 5
res = {0: 0, 1: 0}
written = False
for k in range(F, 621):
    o = hdr + k * rec
    dac = np.asarray(raw[o + 8:o + 776]).astype(np.int64)
    for extra in (0, 1):
        c = (k - F) + extra          # callbacks
        pal = np.clip((src * fade[c]) >> 8, 0, 63)
        if fade[c] != f0 and np.array_equal(pal, dac):
            res[extra] += 1
print('fade start value [0x15020] =', f0, '; records whose DAC matches the fade after n callbacks:', res[0],
      ', after n + 1 callbacks:', res[1], '(of the records with a fade in progress:',
      sum(1 for k in range(F, 621) if fade[k - F + 1] != f0), ')')
