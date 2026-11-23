"""Model of 0x40684c, the credits flight path (1024 entries of 0x6c bytes at [0x426424]):
   +0x00 int x, y, z      the ring centre (cumulative sum of steps of length D = 686)
   +0x0c int light[12]    the vertex light level 0..1008 (shade row = level >> 4)
   +0x3c float radius[12] the vertex radius factor
Run: python3 path.py   compares the model with the table in snaps/f2148.snap."""
import math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

f32 = lambda x: float(np.float32(x))
D = 686                       # [0x426470] = tunnel ring 1 z - ring 0 z after loading tuubi.sro at scale 7
RAND_BEFORE = 260             # rand() calls before this precalc (from Watcom's seed 1), see seed.py


class Rand:
    def __init__(self, skip=RAND_BEFORE):
        self.s = 1
        for _ in range(skip): self()

    def __call__(self):
        self.s = (self.s * 0x41c64e6d + 0x3039) & 0xffffffff
        return (self.s >> 16) & 0x7fff


def trunc(x): return int(math.trunc(x))


# The double constants are the image's (0x420fa4..0x4210d4): 1/x of the decimal divisors written here, except
# three that are one ulp off 1/x and are spelled in hex.
LD = np.float64     # the x87 runs with control word 0x127f (snapshot): precision 53 bits, round to nearest.
                    # With 64-bit intermediates one radius of 12288 comes out one ulp off; with 53 bits all match.


def sin(x): return LD(math.sin(float(x)))     # the emulator computes fsin / fcos in double on the host
def cos(x): return LD(math.cos(float(x)))
def dbl(x): return LD(float(x))               # a store to a qword / load of a double constant
def flt(x):
    """A store to a dword float: round to nearest (even)."""
    x = LD(x)
    f = np.float32(float(x))
    best = f
    for c in (np.nextafter(f, np.float32(-np.inf)), np.nextafter(f, np.float32(np.inf))):
        dc, db = abs(LD(c) - x), abs(LD(best) - x)
        if dc < db or (dc == db and int(np.float32(c).view(np.uint32)) % 2 == 0):
            best = c
    return LD(best)


def build(rand=None):
    """The path exactly as 0x40684c computes it (x87 at 53-bit precision, float stores)."""
    rand = rand or Rand()
    X = Y = Z = LD(0)                                # float accumulators [esp+0x60], [esp+0x54], [esp+0x58]
    a, b, c, d, e, g, h = 111, 15, -10, 30, 60, 20, 10   # all advance by 1 per entry
    K = lambda v: LD(v)                              # double constants (exact as written in the image)
    out = []
    for i in range(1024):
        A = abs(cos(K(g) * dbl(1 / 7.1)) * dbl(5530))
        B = abs(sin(K(a) * dbl(float.fromhex('0x1.4514514514515p-3'))) * dbl(1250))
        C = abs(cos(K(g) * dbl(1 / 10.1)) * dbl(4240))
        E = abs(sin(K(i) * dbl(1 / 10.3)) * dbl(1500))
        Bx = B - C + cos(K(e) * dbl(1 / 28.4)) * dbl(2363)
        Ay = A + E - cos(K(c) * dbl(1 / 14.4)) * dbl(1650)
        Bx = Bx - cos(K(i) * dbl(1 / 14.5)) * dbl(2653)
        Ay = Ay + cos((K(i) + dbl(20.4)) * dbl(1 / 16.5)) * dbl(2757)
        Bx = Bx - sin(K(d) * dbl(1 / 21.3)) * dbl(1100) * dbl(1.2)
        Ay = Ay + abs(sin(K(b) * dbl(1 / 14.3)) * dbl(3130))
        v = [dbl(Ay), dbl(Bx), dbl(D)]                # 0x413510: (P, Q, D) as doubles
        k = dbl(LD(D) / np.sqrt(v[1] * v[1] + v[0] * v[0] + v[2] * v[2]))   # 0x413750(v, D)
        v = [dbl(x * k) for x in v]
        X, Y, Z = flt(X + v[0]), flt(Y + v[1]), flt(Z + v[2])
        r1 = rand() % 900
        base = flt(K(r1) * dbl(float.fromhex('0x1.b4e81b4e81b4ep-11')) + dbl(0.65))
        light, radius = [], []
        for j in range(12):
            k1, k2 = i + j, i - j
            r2 = rand() % 200
            rr = dbl(abs(sin(K(j) * dbl(1 / 3.1)) * dbl(0.4)) + base)
            radius.append(float(flt(K(r2) * dbl(0.001) + rr)))
            s3 = (cos(K(k2) * dbl(1 / 17.1)) * dbl(0.5) + sin(K(i) * dbl(1 / 10.3)) * dbl(0.5)
                  + sin(K(h) * dbl(1 / 13.1)) * dbl(0.8) + cos(K(h) * dbl(float.fromhex('0x1.8cb4ff6151337p-6'))) * dbl(0.2))
            t = abs(s3 * dbl(1 / 3) + sin(K(k1) * dbl(1 / 2.23)))
            t = t + sin((K(i) + dbl(K(j)) * dbl(0.8)) * dbl(1 / 3.05)) * dbl(0.4)
            L = flt(t * dbl(64) * dbl(16))
            L = LD(0) if L < 0 else (LD(1008) if L > 1008 else L)
            light.append(int(L))                     # truncation toward zero (0x40ba78)
        out.append(((int(X), int(Y), int(Z)), light, radius))
        a += 1; b += 1; c += 1; d += 1; e += 1; g += 1; h += 1
    return out


if __name__ == '__main__':
    import snap
    m = snap.Mem('f2148')
    T = m.d32(0x426424)
    model = build()
    bad = [0, 0, 0]
    maxd = 0
    for i, (pos, light, radius) in enumerate(model):
        e = T + i * 0x6c
        tp, tl, tr = m.ints(e, 3), m.ints(e + 12, 12), m.floats(e + 0x3c, 12)
        if tuple(pos) != tp:
            bad[0] += 1; maxd = max(maxd, max(abs(p - q) for p, q in zip(pos, tp)))
        if tuple(light) != tl: bad[1] += 1
        if tuple(radius) != tr: bad[2] += 1
    print('entries differing: position %d (max %d), light %d, radius %d of 1024' % (bad[0], maxd, bad[1], bad[2]))
