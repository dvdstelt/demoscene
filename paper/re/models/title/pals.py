import sys, math, struct
sys.path.insert(0, '.')
import numpy as np
import model
x = 0.0; st = struct.unpack('<f', struct.pack('<f', 0.003067961661145091))[0]; Sx = []
for k in range(1024):
    Sx.append(int(round(math.sin(x)*65536))); x += st; x += st
model.S = np.array(Sx)
for a, sh in [(0, 11), (256, 11), (512, 11), (768, 11), (972, 11), (2910, 11), (3316, 11), (720, 12)]:
    pal, base = model.palette(a, sh)
    print(a, sh, 'C', [int(v) for v in base], 'pal[70]', pal[70].tolist(), 'pal[127]', pal[127].tolist(), 'pal[128]', pal[128].tolist(), 'pal[200]', pal[200].tolist())
