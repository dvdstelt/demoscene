"""Rebuild the twirl tables (0x40a29c pre-table, 0x40a408 conversion) and compare with the snapshot.

pre[y][x] = (u, v) = ((i >> 2) - 128, (j >> 2) - 128) of the last sample (j outer 0..511, i inner 0..1023) that lands
on pixel (x, y): x = trunc(160 + r cos a), y = trunc(100 + r sin a), a = i * 0.006135923 (accumulated in double),
r = j * 0.43 (accumulated in double).  0x40a408: word0 = v & 0xff, word1 = ((v << 8) + (u & 0xff)) & 0xffff.
Writes models/twirl/pre.npz (u, v per pixel, -999 where never written) for the other models.
"""
import math, os, pickle, struct, sys
import numpy as np
import unicorn  # noqa: F401

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))


def trunc(x):
    return int(x)  # toward zero, like 0x40ba78 + fistp


def build_pre(cx=160, cy=100):
    u = np.full((200, 320), -999, np.int32)
    v = np.full((200, 320), -999, np.int32)
    r = 0.0
    for j in range(512):
        a = 0.0
        for i in range(1024):
            x = trunc(math.cos(a) * r + cx)
            if -1 < x < 320:
                y = trunc(math.sin(a) * r + cy)
                if -1 < y < 200:
                    u[y, x] = (i >> 2) - 128
                    v[y, x] = (j >> 2) - 128
            a += 0.006135923
        r += 0.43
    return u, v


def main():
    u, v = build_pre()
    np.savez(os.path.join(HERE, 'pre.npz'), u=u, v=v)
    miss = (u == -999)
    print('pixels never written:', int(miss.sum()))
    uu = np.where(miss, 0, u); vv = np.where(miss, 0, v)   # zeroed allocation
    w0 = vv & 0xff
    w1 = ((vv << 8) + (uu & 0xff)) & 0xffff
    s = pickle.load(open(os.path.join(RE, 'snaps', 'f10574.snap'), 'rb'))
    m = s['mem']
    tab = struct.unpack_from('<I', m, 0x4b6d38)[0]
    t = np.frombuffer(bytes(m[tab:tab + 0x3e800]), '<u2').reshape(200, 320, 2)
    d0 = (t[..., 0] != w0); d1 = (t[..., 1] != w1)
    print('word0 differs:', int(d0.sum()), ' word1 differs:', int(d1.sum()))
    ys, xs = np.nonzero(d0 | d1)
    for y, x in list(zip(ys, xs))[:20]:
        print(f'  ({x},{y}) snap {t[y,x,0]:#06x} {t[y,x,1]:#06x}  model {w0[y,x]:#06x} {w1[y,x]:#06x}  u,v={u[y,x]},{v[y,x]}')


if __name__ == '__main__':
    main()
