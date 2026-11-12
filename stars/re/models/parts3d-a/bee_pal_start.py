"""The bee's fade from white (0x120df, ticks 0..15) against full.frames."""
import numpy as np
from common import frame, ticks, file

P26 = np.frombuffer(file(26), np.uint8).reshape(256, 3).astype(int).copy()
P26[0] = np.frombuffer(file(31), np.uint8)[:3]
tk = ticks()
for f in range(5949, 5966):
    pal = frame(f)[1].astype(int)
    hits = [L for L in range(0, 64) if (((63 + (((P26 - 63) * L) & 0xffff) // 64) & 0xff) == pal).all()]
    print(f, 'pit', tk[f][2], 'levels', hits)
