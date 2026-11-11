import sys, pickle, struct
import numpy as np
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep
O = '/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-logo/'
r = pickle.load(open(O + 'rng.rng', 'rb'))
class RNG:
    def __init__(s, state):
        s.x = list(struct.unpack('<64I', state[:256])); s.j = state[256]; s.k = state[257]
    def __call__(s):
        v = (s.x[s.j] + s.x[s.k]) & 0xffffffff
        s.x[s.k] = v; s.j = (s.j + 1) & 63; s.k = (s.k + 1) & 63
        return v
g0 = RNG(rep.SEG[0x9a0:0xaa2])
print('exe initial idx', rep.SEG[0xaa0], rep.SEG[0xaa1])
for i in range(r['calls_before']): g0()
g1 = RNG(r['state'])
print('state after N calls matches:', g0.x == g1.x and (g0.j, g0.k) == (g1.j, g1.k))
n = np.zeros(0x3ed1 * 4, np.int32)
for i in range(0x3ed1):
    v = g1() & 0x0f0f0f0f
    n[4*i:4*i+4] = [(v >> (8*b)) & 0xff for b in range(4)]
t = n.copy()
for i in range(64000):
    a = (t[i] - t[i + 0x141]) & 0xff
    a = a - 256 if a >= 128 else a
    a = (a >> 3) + 0x1e
    a &= 0xff
    if a > 0x1f: a = 0x1f
    t[i] = a
tex = np.load(O + 'tex.npy')
print('texture regenerated == snapshot:', (t[:64000] == tex).all())
