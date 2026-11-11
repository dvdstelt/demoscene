"""Find which dump/*.tga a 16-bit (5:6:5) 256x256 buffer was converted from (0x13d73: r>>3<<11 | g>>2<<5 | b>>3)."""
import sys, glob, os
import numpy as np
from snap import mem, RE
from PIL import Image
tgas = {os.path.basename(p): np.array(Image.open(p).convert('RGB')).astype(np.uint32)
        for p in sorted(glob.glob(os.path.join(RE, 'dump', '*.tga')))}
def to565(t): return ((t[..., 0] >> 3) << 11) | ((t[..., 1] >> 2) << 5) | (t[..., 2] >> 3)
if __name__ == '__main__':
    tick = int(sys.argv[1]); m = mem(tick)
    for spec in sys.argv[2:]:
        a = int(spec, 0)
        p = m.u32(a) if a < 0x40000 else a
        w = m.arr(p, 65536, '<u2').reshape(256, 256)
        res = []
        for n, t in tgas.items():
            for fl in (0, 1):
                tt = t[::-1] if fl else t
                res.append(((to565(tt) == w).mean(), n, fl))
        res.sort(reverse=True)
        print(spec, hex(p), res[:2])
