"""Identify which generated texture each buffer pointer holds (compare planar RGB with dump/*.tga)."""
import sys, glob, os
import numpy as np
from snap import mem, RE
from PIL import Image

def tga_planes(p):
    im = np.array(Image.open(p).convert('RGB'))
    return im
tgas = {os.path.basename(p): tga_planes(p) for p in sorted(glob.glob(os.path.join(RE, 'dump', '*.tga')))}

def ident(m, ptr):
    a = m.arr(ptr, 0x30000, np.uint8)
    r, g, b = a[:65536].reshape(256, 256), a[65536:131072].reshape(256, 256), a[131072:].reshape(256, 256)
    img = np.stack([r, g, b], -1)
    best = []
    for n, t in tgas.items():
        for flip in (False, True):
            tt = t[::-1] if flip else t
            d = np.abs(tt.astype(int) - img.astype(int)).mean()
            best.append((d, n, flip))
    best.sort()
    return best[:2]

if __name__ == '__main__':
    tick = int(sys.argv[1])
    m = mem(tick)
    for a in [int(x, 0) for x in sys.argv[2:]]:
        p = m.u32(a)
        print(hex(a), hex(p), ident(m, p))
