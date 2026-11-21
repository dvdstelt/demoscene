"""Find where the water init's 24 rand() calls (0x4079f4: per fish r%1000, r%3000, r%300) sit in the sequence that
starts at the flight init's srand(0x45eafc) (0x402967), using the fish positions in snaps/f8468 (after one frame)."""
import snap

m = snap.Mem('f8468')
fish = [m.d32(0x426570 + 4 * i) for i in range(8)]
pos = [m.ints(f + 0x10, 3) for f in fish]
print('fish positions at f8468:', pos)
print('seed at f8468: %#x' % m.d32(0x423818))

seed = 0x45eafc
R = []
for n in range(3_000_000):
    seed = (seed * 0x41c64e6d + 0x3039) & 0xffffffff
    R.append((seed >> 16) & 0x7fff)
hits = []
for n in range(len(R) - 24):
    ok = True
    for i in range(8):
        z0 = R[n + 3 * i] % 1000
        x0 = R[n + 3 * i + 1] % 3000 - 1500
        if abs(pos[i][0] - x0) > 40 or abs(pos[i][2] - z0) > 40:
            ok = False
            break
    if ok:
        hits.append(n)
print('hits (rand calls since srand before the water init):', hits[:5])
for n in hits[:1]:
    for i in range(8):
        print('fish %d: z0 %4d x0 %5d angle %3d deg' % (i, R[n + 3 * i] % 1000, R[n + 3 * i + 1] % 3000 - 1500,
                                                      R[n + 3 * i + 2] % 300))
    # seed value just before the water init
    s = 0x45eafc
    for k in range(n):
        s = (s * 0x41c64e6d + 0x3039) & 0xffffffff
    print('seed before the water init: %#x' % s)
