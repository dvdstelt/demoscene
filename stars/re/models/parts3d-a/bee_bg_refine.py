"""Refine the offsets found by bee_bg_fft.py: index consistency between the cloud render and file 27."""
import numpy as np
from common import file
import bee_bg_fft as B

TEXi = np.frombuffer(file(30), np.uint8)[:65536].astype(np.int64)
T = B.T
rows = slice(1, 198)
p = np.arange(64000)


def consistency(img):
    a = img[rows][B.valid[rows]]; b = B.f27[rows][B.valid[rows]]
    key = a * 256 + b
    cnt = np.bincount(key, minlength=65536).reshape(256, 256)
    return cnt.max(1).sum() / len(a)


res = []
for mode in ('pair', 'exact'):
    for dA1 in range(-2, 3):
        for dD1 in range(-2, 3):
            si = (86 + dA1) * 256 + 108 + dD1
            c1 = TEXi[(T + si) & 0xffff]
            for dA2 in range(-2, 3):
                for dD2 in range(-3, 4):
                    di = (219 + dA2) * 256 + 228 + dD2
                    if mode == 'pair':
                        base = (T[p | 1] & 0xfffe) + (di & 0xfffe)
                        c2 = TEXi[(base + (p & 1)) & 0xffff]
                    else:
                        c2 = TEXi[(T + di) & 0xffff]
                    img = (c1 + 16 * c2).reshape(200, 320)
                    res.append((consistency(img), mode, 86 + dA1, 108 + dD1, 219 + dA2, 228 + dD2))
res.sort(reverse=True)
for r in res[:8]:
    print(r)
