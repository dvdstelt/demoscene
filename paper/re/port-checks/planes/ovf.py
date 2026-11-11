import math, itertools
def s32(x):
    x &= 0xffffffff; return x - (1 << 32) if x >> 31 else x
Lpt = (-1300, 100, -2500)
bad = []
for line in open('teal_dump.txt'):
    a = list(map(int, line.split())); t = a[0]; op = a[6:9]; R = a[9:18]
    m = [R[i] for i in (0, 3, 6, 1, 4, 7, 2, 5, 8)]
    L = [Lpt[q] - op[q] for q in range(3)]
    w = [s32(m[c] * L[0] + m[3 + c] * L[1] + m[6 + c] * L[2]) >> 16 for c in range(3)]
    f = [(m[c] * L[0] + m[3 + c] * L[1] + m[6 + c] * L[2]) / 65536 for c in range(3)]
    nw = math.sqrt(sum(x * x for x in w)) or 1; nf = math.sqrt(sum(x * x for x in f))
    c = sum(x * y for x, y in zip(w, f)) / nw / nf
    if c < 0.999: bad.append(t)
runs = []
for k, g in itertools.groupby(enumerate(bad), lambda x: x[1] - x[0]):
    g = list(g); runs.append((g[0][1], g[-1][1]))
print(len(bad), runs)
