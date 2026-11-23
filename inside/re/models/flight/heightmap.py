"""Model of the flight part's height field (0x401450 heights, 0x40136c light) checked against snapshot f7063.

The height field [0x424ff8] is 128 x 128 entries of 8 bytes {int32 height, int32 light}; entry (row z, column x) at
offset z * 0x400 + x * 8.  usage: python3 heightmap.py [snaps/f7063.snap]  (prints the differences)
"""
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import rep  # noqa: E402

L = np.float64                           # Watcom sets the x87 control word 0x127f: 53-bit precision
HALF_PI = L(rep.rdd(0x420540))          # 1.5707963267948966 (double at 0x420540 / 0x42054c)
INV128 = L(rep.rdf(0x420548))           # 0.0078125 (float)


def s(n, f):
    """0x4012e0: sin(n * pi/2 * f / 128), n int, f float."""
    return np.sin(L(n) * HALF_PI * L(np.float32(f)) * INV128)


def c(n, f):
    """0x401308: cos(n * pi/2 * f / 128)."""
    return np.cos(L(n) * HALF_PI * L(np.float32(f)) * INV128)


def heights():
    h = np.zeros((128, 128), dtype=np.int64)
    for j in range(64):              # row (z)
        for i in range(64):          # column (x)
            acc = np.float64(s(i - 11, 4.0) * L(179.2))
            acc = np.float64(s(j + 64, 2.0) * L(256.0) + L(acc))
            acc = np.float64(c(j - 13, 4.3) * L(179.2) + L(acc))
            t = c(i + 17, 6.0) * L(256.0)
            n = int(L(i * j) * L(0.3))                 # trunc(0.3 * i * j)
            acc = np.float64(t + L(acc))
            acc = np.float64(s(n, 9.0) * L(435.2) + L(acc))
            acc = np.float64(c(i + 10, 16.0) * L(486.4) + L(acc))
            acc = np.float64(c(j - 7, 45.0) * L(128.0) + L(acc))
            acc = np.float64(s(i + 3, 39.0) * L(204.8) + L(acc))
            acc = np.float64(s(j * (i + 1), 64.0) * L(230.4) + L(acc))
            acc = np.float64(c(i * j, 90.0) * L(204.8) + L(acc))
            v = np.float32(s(i * (j + 1), 72.0) * L(153.6) + L(acc))
            if v > 256:
                v = np.float32(0)
            h[j, i] = int(v)                         # truncate toward zero
    h0 = h.copy()
    # 0x40178b: smoothing of rows 0..62, columns 0..62 (in place, reads only entries not yet changed)
    for j in range(63):
        for i in range(63):
            t = h[j, i] + h[j, i + 1] + 2 * h[j + 1, i + 1]
            h[j, i] = int(t / 4) if t >= 0 else -((-t) // 4)
    h[:64, 64:] = h[:64, 63::-1]                     # 0x4017e8: mirror in x
    h[64:, :] = h[63::-1, :]                         # 0x401844: mirror in z
    return h, h0


def light(h):
    """0x40136c: light of entry (z, x) from the slope towards x + 1."""
    out = np.zeros_like(h)
    k = L(rep.rdd(0x420573))                        # -1.0995574287564276 (-0.35 pi)
    for z in range(128):
        for x in range(128):
            d = L(h[z, x] - h[z, (x + 1) % 128]) * L(rep.rdf(0x42055f))   # / 512
            a = np.arctan2(d, L(1))
            v = np.float32(np.cos(a + k) * L(64.0) * L(16.0))
            if v < 160:
                v = np.float32(160)
            out[z, x] = int(v)
    return out


if __name__ == '__main__':
    import unicorn  # noqa: F401  (the snapshot pickle needs it)
    snap = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'snaps', 'f7063.snap')
    mem = pickle.load(open(snap, 'rb'))['mem']
    base = struct.unpack_from('<I', mem, 0x424ff8)[0]
    raw = np.frombuffer(mem, dtype='<i4', count=128 * 128 * 2, offset=base).reshape(128, 128, 2)
    h, h0 = heights()
    lt = light(h)
    dh = raw[:, :, 0] - h
    dl = raw[:, :, 1] - lt
    print('heights: %d of 16384 differ (max %d); range %d..%d' % ((dh != 0).sum(), abs(dh).max(), h.min(), h.max()))
    print('light:   %d of 16384 differ (max %d); range %d..%d' % ((dl != 0).sum(), abs(dl).max(), lt.min(), lt.max()))
