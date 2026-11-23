"""The intro's state at its first frame: drops from the init 0x403770 (seed 1: Watcom's rand() before any srand),
then the seed after every part's init (found by stepping the LCG back from the snapshot), checked against
snaps/f38.snap (the first frame's drop loop has run, its blur is in progress)."""
import os, sys, pickle, struct
import numpy as np
import intro_model as M

INV = pow(0x41c64e6d, -1, 1 << 32)
snap = pickle.load(open(os.path.join(M.HERE, '..', '..', 'snaps', 'f38.snap'), 'rb'))
mem = snap['mem']
u32 = lambda a: struct.unpack_from('<I', mem, a)[0]
seed_snap = u32(0x423818)
drops_snap = M.unpack_drops(mem[u32(0x425568):u32(0x425568) + 0x820])

# the init: 130 drops at (rand() % 320, rand() % 200), intensity 0, from seed 1
r = M.Rng(1)
D0 = []
for i in range(130):
    x = float(r() % 320); y = float(r() % 200)
    D0.append([x, y, 0])
seed_after_intro_init = r.seed
same = sum(1 for a, b in zip(D0, drops_snap) if a == b)
print('init from seed 1: %d of 130 drops equal the snapshot (the others were respawned by the first frame)' % same)
print('seed after the intro init: %#x' % seed_after_intro_init)

# the first frame: drops with X = 200, Y = 150 (init values), count 22, flag 0
found = None
s = seed_snap
for k in range(0, 400):
    D = [d[:] for d in D0]
    rr = M.Rng(s)
    L = np.zeros((200, 320), np.int64)
    M.drops(D, L, 22, 0, {'X': 200.0, 'Y': 150.0}, rr)
    if rr.seed == seed_snap and D == drops_snap:
        found = (k, s, L)
        break
    s = ((s - 0x3039) * INV) & 0xffffffff            # one rand() call back
if found:
    k, s, L = found
    print('first frame: %d rand() calls; seed at the start of the intro\'s first frame = %#x' % (k, s))
    print('deposits of the first frame at', [(int(y), int(x), int(L[y, x])) for y, x in zip(*np.nonzero(L))])
    # how many rand() calls between the intro init and the first frame (the other parts' inits)?
    rr = M.Rng(seed_after_intro_init); n = 0
    while rr.seed != s and n < 10_000_000:
        rr(); n += 1
    print('the seed is %s rand() calls after the intro init (srand(0x45eafc) in the flight init 0x402967 breaks '
          'the chain if not found)' % (n if rr.seed == s else 'not within 10M'))
    rr = M.Rng(0x45eafc); n = 0
    while rr.seed != s and n < 10_000_000:
        rr(); n += 1
    print('... and %s calls after srand(0x45eafc)' % (n if rr.seed == s else 'not within 10M'))
else:
    print('no start seed found')
