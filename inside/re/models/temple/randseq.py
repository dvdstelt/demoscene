"""Find where the temple's flare positions sit in Watcom's rand() sequence (0x4134cd) from the seed 0x45eafc (srand at 0x402967 in the flight init, which runs just before).
Prints the number of rand() calls made before the temple's init and checks the 30 values against snapshot f11276."""
import snap
m = snap.Mem('f11276')
vecs = [tuple(int(v) for v in m.vec(0x425c78 + 0x18 * i)) for i in range(10)]

def rand_stream(seed=0x45eafc):
    while True:
        seed = (seed * 0x41c64e6d + 0x3039) & 0xffffffff
        yield (seed >> 16) & 0x7fff

vals = []
target0 = vecs[0]
g = rand_stream()
hist = []
for n in range(20_000_000):
    hist.append(next(g))
    if len(hist) >= 3:
        a, b, c = hist[-3], hist[-2], hist[-1]
        # call order: a (x?) b (y) c (z?) ; try both assignments
        if (b % 300 + 70) == target0[1]:
            if (c % 1000 - 500, a % 1000 - 500) == (target0[0], target0[2]):
                print('match (x = 3rd call, z = 1st call) at calls', n - 2, '..', n); start = n - 2; order = 'zyx'; break
            if (a % 1000 - 500, c % 1000 - 500) == (target0[0], target0[2]):
                print('match (x = 1st call) at calls', n - 2, '..', n); start = n - 2; order = 'xyz'; break
    if len(hist) > 10: hist.pop(0)
else:
    print('no match'); raise SystemExit
g = rand_stream(); [next(g) for _ in range(start)]
ok = True
for i in range(10):
    a, b, c = next(g), next(g), next(g)
    v = (c % 1000 - 500, b % 300 + 70, a % 1000 - 500) if order == 'zyx' else (a % 1000 - 500, b % 300 + 70, c % 1000 - 500)
    print(i, v, vecs[i], v == vecs[i]); ok &= v == vecs[i]
print('rand() calls before the temple init:', start, 'all match' if ok else 'MISMATCH')
