import numpy as np
from emu import Emu
from model import Fix, s32
e = Emu(9500)
sin = e.ii(0x22479, 1024); F = Fix(sin)
class Rng:
    def __init__(s, e): s.T = [e.d(0x9a0 + 4 * k) for k in range(64)]; s.i = e.rd(0xaa0, 1)[0]; s.j = e.rd(0xaa1, 1)[0]
    def __call__(s):
        x = (s.T[s.i] + s.T[s.j]) & 0xffffffff; s.T[s.j] = x; s.i = (s.i + 1) & 63; s.j = (s.j + 1) & 63; return x
rng = Rng(e)
def rpm(mask):  # 0x377f
    x = rng(); v = x & mask
    return -v if x & 0x10000 else v
def rpm3(mask):  # 0x3793: returns (eax, pop1, pop2) = (third, first, second)
    a = rpm(mask); b = rpm(mask); c = rpm(mask); return c, a, b
base = e.d(0x33c0)
planes = []
for n in range(32):
    rec = e.ii(base + 32 * n, 8); obj = rec[0] & 0xffffffff
    planes.append(dict(obj=obj, p=rec[1:4], yaw=rec[4], pitch=rec[5], spd=rec[6], m=e.ii(obj, 9), wp=e.ii(obj + 0x48, 3)))
tgt = e.ii(0x3390, 3); vel = e.ii(0x33a8, 3); sway = e.ii(0x33c4, 3)  # 33c4, 33c8, 33cc
V = e.ii(0x1e468, 9); cam = e.ii(0x1ea9d, 3)
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
    for p in planes:
        d = [(T[k] - p['wp'][k]) >> 4 for k in range(3)]
        m = p['m']
        r = rpm(0x3f)
        p['yaw'] = -(s32((r << 16) + m[0] * d[0] + m[3] * d[1] + m[6] * d[2]) >> 20)
        r = rpm(0x3f)
        p['pitch'] = -(s32((r << 16) + m[1] * d[0] + m[4] * d[1] + m[7] * d[2]) >> 20)
        p['spd'] += rpm(3) + 12
    sway[0] += 0x64; sway[2] += 0x8f; sway[1] -= 0x48
def f3618():
    global V, cam
    S = [0, 0, 0]
    for p in planes:
        R = F.rotxy(p['pitch'], p['yaw'])
        p['m'] = Fix.mul(p['m'], R)
        sp = p['spd']; p['spd'] = 0
        for k in range(3):
            p['p'][k] = s32(p['p'][k] + p['m'][3 * k + 2] * sp)
            p['wp'][k] = p['p'][k] >> 12
            S[k] += p['wp'][k]
    S = [x >> 5 for x in S]
    S[0] -= cam[0] + cam[1] + cam[2]       # bug: all three camera components subtracted from x only
    cam = [sin[(sway[0] >> 4) & 0x3ff] >> 5, sin[(sway[2] >> 4) & 0x3ff] >> 6, sin[(sway[1] >> 4) & 0x3ff] >> 6]
    c = [x >> 8 for x in S]
    b = s32(V[0] * c[0] + V[1] * c[1] + V[2] * c[2]) >> 15
    a = s32(V[3] * c[0] + V[4] * c[1] + V[5] * c[2]) >> 15
    V = Fix.mul(F.rotz(2), Fix.mul(F.rotxy(a, b), V))
    return a, b, S
for fr in range(4):
    e.call(0x342f); f342f()
    e.call(0x3618); a, b, S = f3618()
    ok_p = all(e.ii(base + 32 * n + 4, 6) == planes[n]['p'] + [planes[n]['yaw'], planes[n]['pitch'], planes[n]['spd']] for n in range(32))
    ok_m = all(e.ii(planes[n]['obj'], 9) == planes[n]['m'] for n in range(32))
    print(fr, 'tgt', tgt == e.ii(0x3390, 3), 'vel', vel == e.ii(0x33a8, 3), 'planes', ok_p, 'mats', ok_m, 'V', V == e.ii(0x1e468, 9),
          'cam', cam == e.ii(0x1ea9d, 3), 'turn a,b', a, b, 'centre', S, tgt, vel)
