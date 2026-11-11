exec(open('v_teal2.py').read().split("for i in range(28): step(None)")[0])
import math
Lpt = (-1300, 100, -2500)
for e in tbl: pass
tb = p['table']; tbl[:] = [[tb[3 * n], tb[3 * n + 1], tb[3 * n + 2]] for n in range(72)]
pos[:] = [0, 0, 0]; hist[:] = [(0, 0, [0] * 9)] * 28; idx = 0; k = 900
def step2(tick):
    global idx, k
    N1, N2, N3 = noise(tbl, 0), noise(tbl, 8), noise(tbl, 16)
    R = Fix.mul(F.rotz(N3 + 0x180), Fix.mul(F.rotx((N2 >> 6) + 25), Fix.mul(F.roty((N1 * 5) >> 3), F.rotx(0x100))))
    pos[0] = s32(pos[0] + ((R[6] * 3) >> 1)); pos[1] = s32(pos[1] + ((R[7] * 3) >> 1))
    h = hist[idx]; hist[idx] = (pos[0], pos[1], R); idx = (idx + 1) % 28
    V = h[2]
    if tick is not None and counter[tick] > 0x2f0 and k: k -= 2
    objpos = [(pos[c] >> 10) + ((V[c] * k) >> 15) - ((V[6 + c] * k) >> 15) for c in range(3)]
    return R, objpos
for i in range(28): step2(None)
bad = []
for tick in range(6018, 7741):
    for e in tbl: e[0] = s32(e[0] + 2 * e[2])
    R, op = step2(tick)
    m = [R[i] for i in (0, 3, 6, 1, 4, 7, 2, 5, 8)]
    L = [Lpt[q] - op[q] for q in range(3)]
    w = [s32(m[c] * L[0] + m[3 + c] * L[1] + m[6 + c] * L[2]) >> 16 for c in range(3)]
    f = [(m[c] * L[0] + m[3 + c] * L[1] + m[6 + c] * L[2]) / 65536 for c in range(3)]
    nw = math.sqrt(sum(x * x for x in w)) or 1; nf = math.sqrt(sum(x * x for x in f))
    cosang = sum(a * b for a, b in zip(w, f)) / nw / nf
    if cosang < 0.999: bad.append((tick, round(math.degrees(math.acos(max(-1, min(1, cosang)))))))
print('ticks with overflowed light direction:', len(bad), bad[:5], bad[-5:])
import itertools
runs = []
for kk, g in itertools.groupby(enumerate(t for t, _ in bad), lambda x: x[1] - x[0]):
    g = list(g); runs.append((g[0][1], g[-1][1]))
print(runs)
