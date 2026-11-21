"""The rand() seed at the start of the water part's first frame in the capture: step the LCG back from the seed in
snaps/f8468 (taken inside the first frame's ripple, after all of its rand() calls) by n calls and keep the n for
which replaying the first frame's calls (8 fish: rand, +1 when r % 300 == 0; 3 drop tries: rand, +3 on a hit)
uses exactly n calls."""
import snap

A, C = 0x41c64e6d, 0x3039
AINV = pow(A, -1, 1 << 32)
m = snap.Mem('f8468')
s_after = m.d32(0x423818)


def back(s, n):
    for _ in range(n):
        s = ((s - C) * AINV) & 0xffffffff
    return s


def calls_from(s):
    def rnd():
        nonlocal s
        s = (s * A + C) & 0xffffffff
        return (s >> 16) & 0x7fff
    n = 0
    for i in range(8):
        n += 1
        if rnd() % 300 == 0:
            rnd(); n += 1
    for k in range(3):
        n += 1
        if rnd() % 200 == 0:
            rnd(); rnd(); rnd(); n += 3
    return n, s


for n in range(11, 30):
    s0 = back(s_after, n)
    used, s1 = calls_from(s0)
    if used == n and s1 == s_after:
        print('consistent: %d calls in the first frame, seed before it %#x' % (n, s0))
