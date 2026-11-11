import json, sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep
from model import Fix, s32
from snap import M
p = json.load(open('../runs/flock.probe.json'))
m9 = M(9500)
sin = m9.ii(0x22479, 1024); F = Fix(sin)
class Rng:
    def __init__(s, st): s.T = list(st[:64]); s.i, s.j = st[64], st[65]
    def __call__(s):
        x = (s.T[s.i] + s.T[s.j]) & 0xffffffff; s.T[s.j] = x; s.i = (s.i + 1) & 63; s.j = (s.j + 1) & 63; return x
rng = Rng(p['rng388b'])
def rpm(mask):
    x = rng(); v = x & mask
    return -v if x & 0x10000 else v
def rpm3(mask):
    a = rpm(mask); b = rpm(mask); c = rpm(mask); return c, a, b
# setup 0x33d0 from the RNG
c, a, b = rpm3(0x3f); tgt = [c, a, b]
planes = []
for n in range(32):
    c, a, b = rpm3(0x3ff)
    planes.append(dict(p=[c << 11, a << 11, b << 10], yaw=0, pitch=0, spd=0, m=[65536, 0, 0, 0, 65536, 0, 0, 0, 65536], wp=[0, 0, 0]))
f0 = p['flock0']
print('setup matches probe:', [x for pl in planes for x in pl['p']] == [f0['recs'][8 * n + k] for n in range(32) for k in (1, 2, 3)], tgt == f0['tgt'])
vel = [rep.rd32(0x33a8 + 4 * k) for k in range(3)]
vel = [v - (1 << 32) if v >> 31 else v for v in vel]
sway = [s32(rep.rd32(0x33c4 + 4 * k)) for k in range(3)]   # 33c4, 33c8, 33cc
print('static initial vel', vel, 'sway', sway)
V = F.rotxy(0x100, 0); cam = [-10000, 0, 0]
def f342f():
    global tgt, vel
    c, a, b = rpm3(1)
    v = [vel[0] + c, vel[1] + a, vel[2] + b]
    if (rng() & 0x3f) == 0:
        c, a, b = rpm3(7); v = [c, a, b]
    v = [max(-7, min(7, x)) for x in v]
    lim = (450, 450, 14)
    n = [tgt[k] + v[k] for k in range(3)]
    for k in range(3):
        if n[k] < -lim[k] or n[k] > lim[k]:
            v[k] = -v[k]; n[k] += v[k]
    tgt, vel = n, v
    T = [x << 4 for x in tgt]
    for q in planes:
        d = [(T[k] - q['wp'][k]) >> 4 for k in range(3)]
        m = q['m']
        q['yaw'] = -(s32((rpm(0x3f) << 16) + m[0] * d[0] + m[3] * d[1] + m[6] * d[2]) >> 20)
        q['pitch'] = -(s32((rpm(0x3f) << 16) + m[1] * d[0] + m[4] * d[1] + m[7] * d[2]) >> 20)
        q['spd'] += rpm(3) + 12
    sway[0] += 0x64; sway[2] += 0x8f; sway[1] -= 0x48
def f3618():
    global V, cam
    S = [0, 0, 0]
    for q in planes:
        q['m'] = Fix.mul(q['m'], F.rotxy(q['pitch'], q['yaw']))
        sp = q['spd']; q['spd'] = 0
        for k in range(3):
            q['p'][k] = s32(q['p'][k] + q['m'][3 * k + 2] * sp); q['wp'][k] = q['p'][k] >> 12; S[k] += q['wp'][k]
    S = [x >> 5 for x in S]; S[0] -= cam[0] + cam[1] + cam[2]
    cam = [sin[(sway[0] >> 4) & 0x3ff] >> 5, sin[(sway[2] >> 4) & 0x3ff] >> 6, sin[(sway[1] >> 4) & 0x3ff] >> 6]
    c = [x >> 8 for x in S]
    b = s32(V[0] * c[0] + V[1] * c[1] + V[2] * c[2]) >> 15
    a = s32(V[3] * c[0] + V[4] * c[1] + V[5] * c[2]) >> 15
    V = Fix.mul(F.rotz(2), Fix.mul(F.rotxy(a, b), V))
f342f()   # the extra call at 0x391c
want_t = m9.ii(0x3390, 3); want_p = [m9.ii(m9.d(0x33c0) + 32 * n + 4, 3) for n in range(32)]
for frame in range(1, 1200):
    f342f()
    if tgt == want_t and [q['p'] for q in planes] == want_p and V == m9.ii(0x1e468, 9) and cam == m9.ii(0x1ea9d, 3):
        print('t9500 snapshot state reached exactly after', frame, '0x342f frame calls (0x3618 calls:', frame - 1, ')'); break
    f3618()
else:
    print('no match')
