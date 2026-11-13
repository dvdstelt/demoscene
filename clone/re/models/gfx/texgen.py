"""Model of the texture precalc in main (0x400240..0x400844) and the grid texture expansion 0x4033a7.

usage (inside the toolbox, from clone/re):
  python3 models/gfx/texgen.py [--snap snaps/t11.snap] [--png dump/textures] [--fyl2x old|new]

Builds, exactly as the original's FPU does it,
  map A [0xe35108], map B [0xe3510c], map C [0xe35110]: 257 rows x 256 texels of dword 0x00RRGGBB + 1 dword 0,
  colour table 1 at 0xe6542b, colour table 2 at 0xe35128: 257 x 256 entries of 3 bytes R, G, B + 3 bytes 0,
  the grid texture 0xdf02ec = 0x4033a7(table) (256 x 256 dwords + 257 dwords repeated from the start),
and compares them byte for byte with a snapshot (the grid texture is table 1's at t11, table 2's from event 5 on).

FPU model (what the emulator, unicorn 2 = QEMU 5.0 softfloat, does with control word 0x1f7f):
- arithmetic (fmul, fadd, fsubr) in 64-bit mantissas, rounded toward zero (truncation);
- fst / fstp to a double or a float: rounded toward zero to 53 / 24 bits;
- fsqrt: exact, truncated (only integer square roots are taken: floor(sqrt(n)) after fistp);
- fsin / fcos: the argument is converted to a double (truncated), the host's libm sin / cos (round to nearest)
  computes the result, which is taken over exactly;
- fyl2x (log): QEMU 5.0: log(double(ST0)) / log(2.0) * double(ST1) in host doubles (round to nearest);
  --fyl2x new models a full-precision log rounded once (to see whether it matters);
- fistp: truncation toward zero.
Numbers are (m, e) = m * 2**e with Python ints.
"""
import math
import os
import pickle
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
import rep  # noqa: E402

FYL2X = 'old'


# ---- a tiny truncating soft float -----------------------------------------------------------------------------
def tr(m, e, bits):
    """m * 2**e rounded toward zero to `bits` significant bits."""
    if m == 0: return (0, 0)
    a = -m if m < 0 else m
    n = a.bit_length()
    if n > bits:
        a >>= n - bits
        e += n - bits
    return (-a if m < 0 else a, e)


def ext(x): return tr(x[0], x[1], 64)
def dbl(x): return tr(x[0], x[1], 53)
def flt(x): return tr(x[0], x[1], 24)


def of(v):
    """Exact (m, e) of a Python float or int."""
    if isinstance(v, int): return (v, 0)
    if v == 0: return (0, 0)
    n, d = v.as_integer_ratio()
    return (n, -(d.bit_length() - 1))


def tofloat(x):
    """Exact Python float of a value with <= 53 bits."""
    return math.ldexp(x[0], x[1])


def mul(a, b): return ext((a[0] * b[0], a[1] + b[1]))


def add(a, b):
    if a[0] == 0: return ext(b)
    if b[0] == 0: return ext(a)
    e = min(a[1], b[1])
    return ext(((a[0] << (a[1] - e)) + (b[0] << (b[1] - e)), e))


def sub(a, b): return add(a, (-b[0], b[1]))


def fsin(x): return of(math.sin(tofloat(dbl(x))))
def fcos(x): return of(math.cos(tofloat(dbl(x))))


def trunc(x):
    m, e = x
    if e >= 0: return m << e
    return -((-m) >> -e) if m < 0 else m >> -e


LN2_EXT = (0xb17217f7d1cf79ac, -64)       # fldln2 (QEMU 5.0 loads the round-to-nearest constant)


def fyl2x(st0, st1):
    if FYL2X == 'old':
        f = tofloat(dbl(st0))
        r = math.log(f) / math.log(2.0)
        r *= tofloat(dbl(st1))
        return of(r)
    # 'new': ln of the extended value, rounded toward zero to 64 bits (approximated through mpmath-free decimal)
    from decimal import Decimal, getcontext
    getcontext().prec = 60
    v = Decimal(st0[0]) * (Decimal(2) ** st0[1])
    ln = v.ln()
    # to (m, e) with 64 bits, truncated
    s = -1 if ln < 0 else 1
    a = abs(ln)
    e = 0
    while a >= 2 ** 64: a /= 2; e += 1
    while a < 2 ** 63: a *= 2; e -= 1
    return (s * int(a), e)


# ---- constants (doubles / floats in the image) ---------------------------------------------------------------
PI = of(rep.rdd(0x41a0a8))
C = {a: of(rep.rdd(a)) for a in (0x41a0b0, 0x41a0b8, 0x41a0c0, 0x41a0d0, 0x41a0d8, 0x41a0e0, 0x41a0e8, 0x41a0f0,
                                 0x41a0f8, 0x41a100, 0x41a108, 0x41a110, 0x41a118, 0x41a120, 0x41a498, 0x41a4a0,
                                 0x41a14c, 0x41a154)}
F_1_256 = of(rep.rdf(0x41a0c8))
F_2 = of(rep.rdf(0x41a0cc))
F_1_128 = of(rep.rdf(0x41a490))
ONE = (1, 0)
K_PI_8, K48, K07, K005, K05, K015, K03, K_PI_32, K56, K037, K2, K042, K_PI_16, K168 = (
    C[0x41a0b0], C[0x41a0b8], C[0x41a0c0], C[0x41a0d0], C[0x41a0d8], C[0x41a0e0], C[0x41a0e8], C[0x41a0f0],
    C[0x41a0f8], C[0x41a100], C[0x41a108], C[0x41a110], C[0x41a118], C[0x41a120])
K90, K128A, K23, K128B = C[0x41a498], C[0x41a4a0], C[0x41a14c], C[0x41a154]

_cache23, _cache90 = {}, {}


def log23(x):
    """0x4034e7(float x) = clamp(trunc(23 ln(1 + x) + 128), 0, 255)."""
    if x in _cache23: return _cache23[x]
    v = add(ONE, x)
    r = fyl2x(v, LN2_EXT)
    r = add(mul(r, K23), K128B)
    i = max(0, min(255, trunc(r)))
    _cache23[x] = i
    return i


def log90(x):
    """0x4167b7(float x) = clamp(trunc(ln((1 + x) / 128) * 90.909 + 128), 0, 255)."""
    if x in _cache90: return _cache90[x]
    v = mul(add(ONE, x), F_1_128)
    r = fyl2x(v, LN2_EXT)
    r = add(mul(r, K90), K128A)
    i = max(0, min(255, trunc(r)))
    _cache90[x] = i
    return i


def cellc(x, y, cell, kpi, amp, sub_from=None):
    """trunc((cos(floor(sqrt(dx^2 + dy^2)) * pi * kpi) + 1) * amp), dx = x mod cell - cell/2 (or 168 - that)."""
    h = cell // 2
    dx, dy = x % cell - h, y % cell - h
    d2 = dy * dy + dx * dx
    r = math.isqrt(d2)                          # fsqrt + fistp (truncating)
    t = fcos(mul(mul((r, 0), PI), kpi))
    t = mul(add(t, ONE), amp)
    if sub_from is not None: t = sub(sub_from, t)   # fsubr
    return trunc(t), d2


def build():
    """Returns mapA, mapB, mapC (bytes, 0x40404 each), table1, table2 (bytes, 257 * 256 * 3 + 3 each)."""
    W, H = 256, 257
    maps = [bytearray(W * H * 4 + 4) for _ in range(3)]
    tabs = [bytearray(W * H * 3 + 3) for _ in range(2)]
    mapA, mapB, mapC = maps
    t1, t2 = tabs
    # per column / row terms: u = x / 256 * 2, v = y / 256 * 2 (exact), su = sin(u pi), sv = sin(v pi)
    U = [mul(mul((x, 0), F_1_256), F_2) for x in range(W)]
    V = [mul(mul((y, 0), F_1_256), F_2) for y in range(H)]
    SU = [fsin(mul(u, PI)) for u in U]
    SV = [fsin(mul(v, PI)) for v in V]

    def first(x, k, a0):            # fld u; fmul pi; fsin; fmul k; ... fadd a0; ... fstp qword (truncated double)
        return dbl(add(mul(SU[x], k), a0))

    A = {key: [first(x, *key) for x in range(W)]
         for key in ((K005, K05), (K015, K07), (K005, K07), (K005, K042))}
    B = {k: [mul(SV[y], k) for y in range(H)] for k in (K005, K015)}

    def chan(x, y, k, a0, c):       # (sin(v pi) k + [sin(u pi) k + a0]) * c, to float
        return flt(mul(add(B[k][y], A[(k, a0)][x]), c))

    for y in range(H):
        for x in range(W):
            i3, i4 = (y * W + x) * 3, (y * W + x) * 4
            # 8-texel cells: table 1 and map C
            c8, _ = cellc(x, y, 8, K_PI_8, K48)
            c = (c8, 0)
            t1[i3] = log23(flt(mul(c, K07)))
            t1[i3 + 1] = log23(chan(x, y, K005, K05, c))
            t1[i3 + 2] = log23(chan(x, y, K015, K07, c))
            mapC[i4] = log90(flt(mul((c8 * 5, 0), K03)))
            mapC[i4 + 2] = log90(chan(x, y, K005, K07, c))
            mapC[i4 + 1] = log90(chan(x, y, K015, K07, c))
            # 32-texel cells: table 2
            c32, d2_32 = cellc(x, y, 32, K_PI_32, K56)
            c = (c32, 0)
            t2[i3] = log23(flt(mul(mul(c, K037), K2)))
            t2[i3 + 1] = log23(chan(x, y, K005, K042, c))
            t2[i3 + 2] = log23(chan(x, y, K015, K07, c))
            # 16-texel cells: map B
            c16, _ = cellc(x, y, 16, K_PI_16, K56)
            c = (c16, 0)
            mapB[i4] = log90(flt(mul((c16 * 5, 0), K03)))
            mapB[i4 + 1] = log90(chan(x, y, K005, K07, c))
            mapB[i4 + 2] = log90(chan(x, y, K015, K07, c))
            # 32-texel cells, inverted: map A
            cA, _ = cellc(x, y, 32, K_PI_32, K56, sub_from=K168)
            c = (cA, 0)
            mapA[i4] = log90(flt(mul((cA * 5, 0), K03)))
            mapA[i4 + 1] = log90(chan(x, y, K005, K07, c))
            mapA[i4 + 2] = log90(chan(x, y, K015, K07, c))
    return mapA, mapB, mapC, t1, t2


def grid_texture(table):
    """0x4033a7: 0x10000 dwords (R << 16 | G << 8 | B from the table's first 256 rows), then the first 0x101
    dwords again at +0x40000. EAX is cleared only before the loop and shifted left by 16 after AL = R, so the top
    byte of each dword is the previous entry's G (0 for the first)."""
    out = bytearray(0x40404)
    prev_g = 0
    for i in range(0x10000):
        r, g, b = table[i * 3:i * 3 + 3]
        struct.pack_into('<I', out, i * 4, prev_g << 24 | r << 16 | g << 8 | b)
        prev_g = g
    out[0x40000:0x40404] = out[0:0x404]
    return out


def compare(name, mine, theirs):
    diff = [i for i in range(len(mine)) if mine[i] != theirs[i]]
    if not diff:
        print(f'{name}: identical ({len(mine)} bytes)')
        return
    mx = max(abs(mine[i] - theirs[i]) for i in diff)
    print(f'{name}: {len(diff)} bytes differ (max {mx}), first at {diff[:8]}')


def main():
    global FYL2X
    args = sys.argv[1:]
    if '--fyl2x' in args: FYL2X = args[args.index('--fyl2x') + 1]
    mapA, mapB, mapC, t1, t2 = build()
    if '--snap' in args:
        import unicorn  # noqa: F401
        snap = pickle.load(open(args[args.index('--snap') + 1], 'rb'))
        mem = snap['mem']
        p = lambda a: struct.unpack_from('<I', mem, a)[0]
        for name, mine, ptr in (('map A', mapA, 0xe35108), ('map B', mapB, 0xe3510c), ('map C', mapC, 0xe35110)):
            a = p(ptr)
            compare(f'{name} [{ptr:#x}] = {a:#x}', mine, mem[a:a + len(mine)])
        compare('table 1 0xe6542b', t1, mem[0xe6542b:0xe6542b + len(t1)])
        compare('table 2 0xe35128', t2, mem[0xe35128:0xe35128 + len(t2)])
        g = p(0x4207c8)
        snapg = mem[g:g + 0x40404]
        for name, t in (('table 1', t1), ('table 2', t2)):
            compare(f'grid texture {g:#x} from {name}', grid_texture(t), snapg)
    if '--png' in args:
        from PIL import Image
        out = args[args.index('--png') + 1]
        os.makedirs(out, exist_ok=True)
        for name, m in (('mapA', mapA), ('mapB', mapB), ('mapC', mapC)):
            px = bytes(b for i in range(257 * 256) for b in (m[i * 4 + 2], m[i * 4 + 1], m[i * 4]))
            Image.frombytes('RGB', (256, 257), px).save(os.path.join(out, f'{name}.png'))
        for name, t in (('table1', t1), ('table2', t2)):
            Image.frombytes('RGB', (256, 257), bytes(t[:257 * 256 * 3])).save(os.path.join(out, f'{name}.png'))


if __name__ == '__main__':
    main()
