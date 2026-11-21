"""Replay the water init's fish placement (0x4079f4) from the seed 0xef012bc7 (39 rand() calls after the flight
init's srand(0x45eafc)) and the first frame's fish update, and compare with snaps/f8468 (inside the first frame)."""
import math
import snap
from water_model import Rand, rot_y, fish_update, PI, DEG
import numpy as np


def rot_x(m, a):
    """0x414270: M = M x Rx(a), Rx = [[1,0,0],[0,c,-s],[0,s,c]]"""
    c, s = math.cos(a), math.sin(a)
    m = list(m)
    for r in range(3):
        m1, m2 = m[3 * r + 1], m[3 * r + 2]
        m[3 * r + 1], m[3 * r + 2] = m1 * c + m2 * s, m2 * c - m1 * s
    return m


def init_fish(seed=0xef012bc7):
    rnd = Rand(seed)
    fish = []
    for i in range(8):
        z = rnd() % 1000
        x = rnd() % 3000 - 1500
        m = rot_x([1, 0, 0, 0, 1, 0, 0, 0, 1], 1.5707963268)            # 0x4108dc(fish, pi/2, 0, 0)
        m = rot_y(m, 0.0) ; m = m                                          # (Ry(0), Rz(0): no change)
        m = rot_y(m, (rnd() % 300) * DEG)                                  # 0x4145ac(fish+0x28, (rand()%300) deg)
        fish.append({'pos': [x, 0, z], 'mat': m})
    return fish, rnd.seed


if __name__ == '__main__':
    fish, s = init_fish()
    print('seed after the init %#x' % s)
    for i, f in enumerate(fish):
        print('fish %d: pos %s, heading %.1f deg' % (i, f['pos'], math.degrees(math.atan2(f['mat'][2], f['mat'][0]))))
    m = snap.Mem('f8468')
    for c in range(33715, 33722):
        ok = True
        dm = 0
        for i in range(8):
            f = {'pos': list(fish[i]['pos']), 'mat': list(fish[i]['mat']), 'verts': [[0] * 16 for _ in range(8)]}
            buf = np.zeros(64000, np.int16)
            log = {'a': [], 's': [], 'proj': []}
            fish_update(i, f, c, Rand(0xc249a56a), buf, log)
            o = m.d32(0x426570 + 4 * i)
            if f['pos'] != list(m.ints(o + 0x10, 3)):
                ok = False
            dm = max(dm, max(abs(a - b) for a, b in zip(f['mat'], m.f64s(o + 0x28, 9))))
        print('first frame with c = %d: positions %s, matrices max diff %.2g' % (c, 'exact' if ok else 'differ', dm))
