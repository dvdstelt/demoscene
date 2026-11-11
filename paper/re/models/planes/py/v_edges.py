import sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep
from emu import Emu
S = rep.SEG
def parse(a):
    nv, nf = S[a], S[a + 1]; p = a + 2 + 3 * nv
    return nv, [tuple(S[p + 4 * i:p + 4 * i + 4]) for i in range(nf)]
for tick, src in ((7000, 0x187d5), (9500, 0x1878b)):
    e = Emu(tick)
    ob = e.d(0x6816) if tick < 8000 else e.d(e.d(0x33c0))
    nv, F = parse(src)
    fl = e.d(ob + 0x74)
    N = [e.ii(fl + 0x4c * k + 0x24, 3) for k in range(len(F))]
    nb_all = []
    ok = True
    for i, f in enumerate(F):
        flags = (f[3] & 7) << 3
        nbs = []
        for q in range(3):
            a, b = f[q], f[(q + 1) % 3]
            j = next((j for j, g in enumerate(F) if j != i and any(g[r] == b and g[(r + 1) % 3] == a for r in range(3))), None)
            # 0x3f37 checks ([ebp]==vb && [ebp+4]==va) || ([ebp+4]==vb && [ebp+8]==va) || ([ebp+8]==vb && [ebp]==va)
            if j is None: nbs.append(i); continue
            nbs.append(j)
            if sum(x * y for x, y in zip(N[i], N[j])) <= 10000: flags |= 8 << q
        emu_flags = e.rd(fl + 0x4c * i + 0x1d, 1)[0] & 0x38
        emu_nb = [(e.d(fl + 0x4c * i + 0xc + 4 * q) - fl) // 0x4c for q in range(3)]
        if emu_flags != flags or emu_nb != nbs: ok = False; print('mismatch', i, f, hex(emu_flags), hex(flags), emu_nb, nbs)
        nb_all.append(nbs)
    print(tick, 'static edge flags + neighbours match:', ok)
    if tick == 7000:
        st = [(i, F[i][:3], hex(e.rd(fl + 0x4c * i + 0x1d, 1)[0] & 0x38)) for i in range(len(F)) if e.rd(fl + 0x4c * i + 0x1d, 1)[0] & 0x38]
        print('teal faces with static edges:', len(st), st[:60])
