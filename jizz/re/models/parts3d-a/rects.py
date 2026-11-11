"""Model of the 'spinning rectangles': 0x18228 (corner table from a key-frame config) + 0x183a9 (draws rectangles
first..last with random jitter, RNG 0x141f6) + the row -> (first, last) selection in the two parts.
Checks against p1_hooks.pkl (event 300) and p2_hooks.pkl (event 510)."""
import math, pickle, struct, sys
import numpy as np
from lines import line
from overlay import decay, blur
F = np.float32
STEP = 2 * math.pi / 16384
M32 = 0xffffffff
CONFIGS = {   # 9 dwords: x(A,B), y(A,B), R(A,B), angle(A,B), n ; then inset x, y (read from the exe at run time)
    'precalc 0x181f4': ((110, 218), (130, 75), (80, 124), (0, -4096), 7, None),
    '515 0x18325': ((150, 110), (55, 70), (70, 113), (0, 4096), 6, (30, 20)),
    '525 0x18351': ((70, 155), (85, 130), (70, 113), (0, -4096), 6, (70, 80)),
    '535 0x1837d': ((70, 160), (85, 100), (70, 113), (0, 4096), 6, (80, 50)),
}

def idiv(a, b):  # x86 idiv: truncate toward zero
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q

def corners(cfg):
    xs, ys, rs, angs, n, _ = cfg
    out = []
    for i in range(n, -1, -1):
        X, Y, R, A = (idiv(p[0] * i + p[1] * (n - i), n) for p in (xs, ys, rs, angs))
        th = F(F(A) + F(2048)) * STEP        # fadd 2048.0 (float), fmul double
        th = float(th) if False else (float(F(A) + F(2048)) * STEP)
        s = F(F(math.sin(th)) * R); c = F(F(math.cos(th)) * R)
        x0 = int(np.rint(F(X + c))); x2 = int(np.rint(F(X - c))); x3 = int(np.rint(F(X + s))); x1 = int(np.rint(F(X - s)))
        s2 = F(s * F(0.625)); c2 = F(c * F(0.625))
        y0 = int(np.rint(F(Y + s2))); y2 = int(np.rint(F(Y - s2))); y1 = int(np.rint(F(Y + c2))); y3 = int(np.rint(F(Y - c2)))
        out.append([x0, y0, x1, y1, x2, y2, x3, y3])
    return out

class Rng:
    """0x141f6, state [0x141ea], [0x141ee], [0x141f2]; returns the new [0x141f2]."""
    def __init__(self, a, b, c): self.s = [a, b, c]
    def __call__(self):
        eax, ebx, ecx = self.s
        rol = lambda v, k: ((v << k) | (v >> (32 - k))) & M32
        eax = (eax + 0xe372471) & M32; ebx = (ebx + 0xbc5ea9) & M32; ecx = (ecx + 0xc8ade7) & M32
        eax ^= ebx
        sa = eax - (1 << 32) if eax >> 31 else eax; sc = ecx - (1 << 32) if ecx >> 31 else ecx
        p = (sa * sc) & 0xffffffffffffffff
        eax, edx = p & M32, p >> 32
        edx = rol(edx, 13); edx = (edx + eax) & M32
        ecx = rol(ecx, 14); edx = (edx - ebx) & M32
        eax ^= edx; ebx = (ebx + ecx) & M32; ecx ^= edx; eax = (eax - ebx) & M32
        ebx = rol(ebx, 9)
        p = eax * ecx; eax, edx = p & M32, p >> 32
        ebx ^= edx
        eax = (eax + 0x1e42ac3) & M32; ebx = (ebx + 0x5edf04) & M32; ecx = (ecx + 0x11a2ec1) & M32
        self.s = [ebx, ecx, eax]
        return eax

def draw(buf, rects, first, last, rng, mask=7):
    """0x183a9 with ebp = mask: four edges per rectangle, corner c -> corner (c+1)&3, one RNG value per edge,
    its nibbles (ror 4 between) jitter x1, y1, x2, y2 by (value & mask)."""
    for k in range(first, last + 1):
        r = rects[k]
        c = 0
        while True:
            v = rng()
            j = []
            for _ in range(4):
                j.append(v & mask); v = ((v >> 4) | (v << 28)) & M32
            x1 = r[c] + j[0]; y1 = r[c + 1] + j[1]
            c = (c + 2) & 7
            x2 = r[c] + j[2]; y2 = r[c + 1] + j[3]
            line(buf, x1, y1, x2, y2)
            if c == 0: break

def select_p1(row):   # part 0x17003, event 300 (0x1731a)
    return (min(row, 14) - 7, 7) if row >= 7 else (0, row)

def select_p2(row):   # part 0x169e7, event 510 (0x16b41)
    a = (row & 15) * 2
    return (min(a, 12) - 6, 6) if a >= 6 else (0, a)

if __name__ == '__main__':
    for pk, cfgname in (('p1_hooks.pkl', None), ('p2_hooks.pkl', None)):
        rec = pickle.load(open(pk, 'rb'))
        tabs = {}
        for t, fl, first, last, tab, rs in rec['rects']:
            tabs.setdefault(tab, []).append(t)
        for tab, ts in tabs.items():
            got = [list(struct.unpack_from('<8i', tab, 32 * k)) for k in range(8)]
            names = [nm for nm, cfg in CONFIGS.items() if corners(cfg) == got[:cfg[4] + 1]]
            print(pk, 'rect table used at ticks', ts[0], '..', ts[-1], 'matches config', names)
        # jitter + drawing: pre buffer of a frame = processed previous buffer + rectangles
        pre = {e[2]: e for e in rec['buf'] if e[0] == 'pre'}
        rr = {fl: (first, last, tab, rs) for t, fl, first, last, tab, rs in rec['rects']}
        for fl in sorted(pre):
            if fl + 1 not in pre or fl + 1 not in rr: continue
            e = pre[fl]
            a = decay(np.frombuffer(e[3][:0x10000], np.uint8))
            if e[4][1]: a = blur(a)
            a = a.copy()
            first, last, tab, rs = rr[fl + 1]
            rects = [list(struct.unpack_from('<8i', tab, 32 * k)) for k in range(8)]
            draw(a, rects, first, last, Rng(*struct.unpack('<3I', rs)))
            nxt = np.frombuffer(pre[fl + 1][3][:0x10000], np.uint8)
            print('  flip', fl + 1, 'rects', first, '..', last, 'pre buffer exact: %.6f' % (a[:64000] == nxt[:64000]).mean())
