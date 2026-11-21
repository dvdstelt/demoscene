"""Recover the rand() outputs behind the credits flight path (0x40684c: per entry rand() % 900, then 12 x rand() %
200) from the table in a snapshot, and find how many rand() calls came before it, starting from Watcom's seed 1."""
import math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import snap

m = snap.Mem('f2148')
P = m.d32(0x426424)
f32 = lambda x: float(np.float32(x))
outs = []
for i in range(4):
    rad = m.floats(P + i * 0x6c + 0x3c, 12)
    found = []
    for r1 in range(900):
        base = f32(r1 * (1 / 1200) + 0.65)          # 0.000833333333 (double constant) then stored as float
        rr = [base + abs(math.sin(j / 3.1) * 0.4) for j in range(12)]
        r2 = [round((rad[j] - rr[j]) / 0.001) for j in range(12)]
        if all(0 <= x < 200 for x in r2) and all(f32(rr[j] + 0.001 * r2[j]) == rad[j] for j in range(12)):
            found.append((r1, r2))
    print(i, found[:3], len(found))
    outs += [found[0][0]] + found[0][1]

# search the LCG: the radius floats of entries 0..3 must come out exactly
s = 1
seq = []
for n in range(400000):
    s = (s * 0x41c64e6d + 0x3039) & 0xffffffff
    seq.append((s >> 16) & 0x7fff)
rad = [m.floats(P + i * 0x6c + 0x3c, 12) for i in range(4)]


def radius(start, i, j):
    r1 = seq[start + 13 * i] % 900
    r2 = seq[start + 13 * i + 1 + j] % 200
    base = f32(r1 * 0.000833333333 + 0.65)
    return f32(r2 * 0.001 + (abs(math.sin(j * 0.322580645) * 0.4) + base))


for start in range(len(seq) - 60):
    if all(radius(start, i, j) == rad[i][j] for i in range(4) for j in range(12)):
        print('rand() calls before the credits path: %d' % start)
        break
else:
    print('not found')
