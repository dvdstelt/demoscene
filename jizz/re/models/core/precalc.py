"""precalc.py: Python models of main's precalc tables, compared with the t430 snapshot (precalc done).
   0x6592 spot table [0x654a], 0x6b16 1/sqrt table 0x671a, 0x6cc0 square table 0x7b9e, 0x10fa2 sine table 0xfb9e,
   0x16f4f polar table [0x16f4b], 0x11dfe random table 0x11666/0x115c6 (+ the fill of [0x652a]),
   0x6cdd marching-cubes case table [0x10fd2]. x87 runs with 24-bit precision: modelled with float32 rounding
   after each basic operation (fsqrt / fadd / fmul / fdiv); fsin and fpatan are computed in double and rounded
   like the FPU would (to 24 bits, then fistp rounds to nearest even)."""
import sys, struct, math
import numpy as np
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re/models/core')
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re')
from snap import load, rd32, rdb
import rep
from rng import Rng

f32 = np.float32
def r32(x): return float(np.float32(x))
def fistp(x):   # round to nearest even, like fistp with RC = nearest
    return int(np.rint(x))

s = load(430)
mem = s['mem']
def cmp(name, got, want):
    got = np.asarray(got); want = np.asarray(want)
    d = np.count_nonzero(got != want)
    print(f'{name}: {got.size} values, {d} differ' + (f' (max |diff| {np.abs(got.astype(np.int64) - want.astype(np.int64)).max()})' if d else ''))
    return d

# 0x6592: spot table, 256x256 bytes at [0x654a]; row y = -128..127, column x = -127..128
def spot():
    out = np.zeros((256, 256), np.uint8)
    for j in range(256):
        y = f32(-128 + j)
        y2 = f32(y * y)
        for i in range(256):
            x = f32(-127 + i)
            r = np.sqrt(f32(f32(x * x) + y2), dtype=np.float32)
            v = f32(f32(255) - r)
            v = f32(v * v); v = f32(v * f32(1 / 256)); v = f32(v * v); v = f32(v * f32(1 / 256))
            out[j, i] = fistp(v) & 0xff
    return out
want = np.frombuffer(rdb(s, rd32(s, 0x654a), 0x10000), np.uint8).reshape(256, 256)
cmp('0x6592 spot [0x654a]', spot(), want)
# continuous form: (255 - r)^4 / 2^24
xs = np.arange(-127, 129); ys = np.arange(-128, 128)
r = np.hypot(xs[None, :], ys[:, None])
cmp('0x6592 spot, plain double formula', np.rint((255 - r) ** 4 / 2 ** 24).astype(np.int64) & 0xff, want)

# 0x6b16: 255 dwords at 0x671a: round(4096 / sqrt(i)), i = 1..255
got = [fistp(f32(4096) / np.sqrt(f32(i), dtype=np.float32)) for i in range(1, 256)]
cmp('0x6b16 rsqrt 0x671a', got, struct.unpack_from('<255i', mem, 0x671a))

# 0x6cc0: 0x2000 dwords at 0x7b9e: k*k, k = -4096..4095 (so sq(k) = [0xbb9e + 4k])
cmp('0x6cc0 squares 0x7b9e', [(k * k) & 0xffffffff for k in range(-4096, 4096)], struct.unpack_from('<8192I', mem, 0x7b9e))

# 0x10fa2: 0x500 dwords at 0xfb9e: round(1024 * sin(2 pi i / 1024)), i = 0..1279
c = f32(rep.rdd(0x11596))
got = [fistp(f32(math.sin(float(f32(f32(16 * i) * c)))) * 1024) for i in range(0x500)]
want = struct.unpack_from('<1280i', mem, 0xfb9e)
cmp('0x10fa2 sine 0xfb9e', got, want)
cmp('0x10fa2 sine, plain round(1024 sin(2 pi i/1024))', [round(1024 * math.sin(2 * math.pi * i / 1024)) for i in range(0x500)], want)

# 0x16f4f: 340 rows x 600 dwords at [0x16f4b]: low byte = angle (512 per turn) mod 256, byte 1 = 1.3 * radius mod 256
def polar():
    out = np.zeros((340, 600), np.uint32)
    for j in range(340):
        y = f32(-169.5 + j)
        for i in range(600):
            x = f32(-299.5 + i)
            a = f32(math.atan2(float(y), float(x)))
            a = f32(f32(a / f32(rep.rdf(0x141e2))) * f32(2))
            rr = f32(np.sqrt(f32(f32(x * x) + f32(y * y)), dtype=np.float32) * f32(1.3))
            out[j, i] = (fistp(a) & 0xff) | ((fistp(rr) & 0xff) << 8)
    return out
want = np.frombuffer(rdb(s, rd32(s, 0x16f4b), 340 * 600 * 4), np.uint32).reshape(340, 600)
P = polar()
cmp('0x16f4f polar [0x16f4b]', P, want)
print('   0x141e2 =', rep.rdf(0x141e2), '2pi/256 =', 2 * math.pi / 256)

# 0x11dfe: [0x652a] filled with 3 (128 KB), then 40 x (rnd -> 0x11666[i], rnd -> 0x115c6[i])
g = Rng()
a, b = [], []
for i in range(40):
    a.append(g.next()); b.append(g.next())
cmp('0x11dfe random 0x11666', a, struct.unpack_from('<40I', mem, 0x11666))
cmp('0x11dfe random 0x115c6', b, struct.unpack_from('<40I', mem, 0x115c6))
print('   rng state after 0x11dfe (at t430 the texture builder has reseeded it):', [hex(x) for x in g.state()])

# 0x6cdd: marching cubes cases, 256 x 128 bytes at [0x10fd2] (aligned up to 256)
def mc():
    out = bytearray(256 * 128)
    edge = [rep.rd32(0x6e0c + 4 * e) for e in range(16)]
    faces = [(0xf0, 0x70e3), (0x0f, 0x7113), (0x33, 0x70aa), (0xcc, 0x7079), (0x55, 0x701d), (0xaa, 0x704b)]
    for c in range(256):
        src = rep.SEG[0x28ed8 + 8 * c:0x28ed8 + 8 * c + 8]
        n = src[0] & 7
        words = [n]
        if n:
            nib = []
            for k in range(1, 16): nib += [src[k // 2] >> 4 if k % 2 else src[k // 2] & 15]
            words += [edge[e] for e in nib[:3 * n]]
            fl = [addr for m, addr in faces if (c & m) not in (0, m)]
            words += [len(fl)] + fl
        struct.pack_into(f'<{len(words)}I', out, 128 * c, *words)
    return out
base = rd32(s, 0x10fd2)
got = np.frombuffer(mc(), np.uint8); want = np.frombuffer(rdb(s, base, 256 * 128), np.uint8)
cmp('0x6cdd marching-cube cases [0x10fd2]', got, want)
