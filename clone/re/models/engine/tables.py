"""Check the engine's precalc tables (0x416806) in a snapshot against their formulas, and look at the maps' layout.

usage: python3 tables.py [../../snaps/t11.snap]
- 0x426b8c, 512 bytes: T[d] = min(255, trunc(128 exp(0.011 (d - 256))))   (the log-space shade table)
- 0x426d8c, 16 x 16 x 4 floats: entry (j, i) = (16-i)(16-j), (16-i) j, i (16-j), i j   (bilinear weights)
- maps A / B / C ([0xe35108], [0xe3510c], [0xe35110]): 256 x 257 dwords + 1; is row 256 a copy of row 0?
"""
import math
import os
import pickle
import struct
import sys

import numpy as np
import unicorn  # noqa: F401  (unpickling the snapshot's context)

HERE = os.path.dirname(os.path.abspath(__file__))
snap = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'snaps', 't11.snap')
mem = pickle.load(open(snap, 'rb'))['mem']
T = np.frombuffer(mem[0x426b8c:0x426b8c + 512], np.uint8)
want = np.array([min(255, int(128 * math.exp(0.011 * (d - 256)))) for d in range(512)])
print('T: %d of 512 entries differ from the formula; T[0] %d, T[128] %d, T[256] %d, first 255 at %d' % (
    (T != want).sum(), T[0], T[128], T[256], int(np.argmax(T == 255))))
print('   bytes after T (read when shade + channel >= 512):', mem[0x426d8c:0x426d8c + 32].hex())
W = np.frombuffer(mem[0x426d8c:0x426d8c + 4096], '<f4').reshape(16, 16, 4)
wt = np.array([[[(16 - i) * (16 - j), (16 - i) * j, i * (16 - j), i * j] for i in range(16)] for j in range(16)])
print('weights: %d of 1024 differ' % (W != wt).sum())
for name, ptr in (('A', 0xe35108), ('B', 0xe3510c), ('C', 0xe35110)):
    a = struct.unpack('<I', mem[ptr:ptr + 4])[0]
    m = np.frombuffer(mem[a:a + 4 * (256 * 257 + 1)], '<u4')
    rows = m[:256 * 257].reshape(257, 256)
    print('map %s at %#x: row 256 == row 0: %s; last dword %#x; column 255 == column 0: %s; top byte used: %s' % (
        name, a, np.array_equal(rows[256], rows[0]), m[-1], np.array_equal(rows[:, 255], rows[:, 0]),
        bool((m >> 24).any())))
    for c, sh in (('R', 16), ('G', 8), ('B', 0)):
        ch = (rows[:256] >> sh) & 255
        print('   %s: min %d max %d mean %.1f' % (c, ch.min(), ch.max(), ch.mean()))
