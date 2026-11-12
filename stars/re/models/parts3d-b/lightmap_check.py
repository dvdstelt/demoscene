"""The light map 0x7873a builds at [0x55914] (256x256), compared with the dumps:

  dx = x - 128 if x >= 128 else 127 - x;  dy likewise;  d = min(127, isqrt(dx^2 + dy^2))
  v  = L[127 - d]                      (L = 128 bytes at EBP: 0x9df0 in 0xa14d, 0x16c20 in 0x16d86)
  mode DL = 4 (0xa14d): (v >> 1) << 3;  DL = 2 (0x16d86): v >> 1, then 0x16f1a adds 0x20 to every byte
  (part 0x16f7c adds another 0x20 to 16 rows at 0x88 + [0x16d7e] while drawing; the dump shows that band)
"""
import math
import pickle
import sys

import numpy as np

import snap


def lightmap(table_addr, dl):
    L = np.frombuffer(snap.IMG, np.uint8, 128, table_addr).astype(np.int64)
    x = np.arange(256)
    dx = np.where(x >= 128, x - 128, 127 - x)
    d2 = dx[None, :] ** 2 + dx[:, None] ** 2
    d = np.minimum(127, np.array([[math.isqrt(int(v)) for v in row] for row in d2]))
    v = L[127 - d]
    if dl == 4: return (v >> 1) << 3
    if dl == 2: return v >> 1
    raise ValueError


if __name__ == '__main__':
    a = lightmap(0x9df0, 4)
    m = snap.Mem(pickle.load(open('out/runs/tex_dump200.pkl', 'rb'))['mem'])
    got = m.arr(0x582d0, 65536).reshape(256, 256).astype(np.int64)
    print('0xa14d light map (DL 4, table 0x9df0): %d of 65536 bytes differ' % (got != a).sum())
    b = lightmap(0x16c20, 2) + 0x20
    m = snap.Mem(pickle.load(open('out/runs/torso_dump400.pkl', 'rb'))['mem'])
    got = m.arr(0x582d0, 65536).reshape(256, 256).astype(np.int64)
    ry = m.s32(0x16d7e)
    band = np.zeros((256, 256), bool); band[0x88 + ry:0x88 + ry + 16] = True
    print('0x16d86 light map (DL 2, table 0x16c20, + 0x20): %d bytes differ outside the band, %d inside the band '
          '(rows %d..%d) after adding 0x20 there' % (((got != b) & ~band).sum(), ((got != b + 0x20) & band).sum(),
                                                     0x88 + ry, 0x88 + ry + 15))
