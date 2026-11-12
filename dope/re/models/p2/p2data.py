"""Quick look at p2's data: RIX3 pictures, value histograms."""
import sys, struct
import numpy as np
sys.path.insert(0, '../..')
import os
HERE = os.path.dirname(os.path.abspath(__file__))
img = open(os.path.join(HERE, '../../unpacked/p2.bin'), 'rb').read()

def rix(off):
    assert img[off:off+4] == b'RIX3', img[off:off+4]
    w, h, t = struct.unpack_from('<HHH', img, off + 4)
    pal = np.frombuffer(img[off+10:off+10+768], np.uint8).reshape(256, 3)
    pix = np.frombuffer(img[off+0x30a:off+0x30a+w*h], np.uint8).reshape(h, w)
    return w, h, t, pal, pix

for name, off in [('credits', 0x4510), ('label', 0x2bd20), ('tex', 0x65076)]:
    w, h, t, pal, pix = rix(off)
    vals, cnt = np.unique(pix, return_counts=True)
    print(name, hex(off), w, h, hex(t), 'pixel range', pix.min(), pix.max(), 'n distinct', len(vals))
    print('  values:', list(zip(vals.tolist(), cnt.tolist()))[:80])
    nz = [i for i in range(256) if pal[i].any()]
    print('  palette nonzero entries:', nz[:5], '...', nz[-5:], len(nz))

pal = np.frombuffer(img[0x65080:0x65080+720] + img[0x2bd20+0x2da:0x2bd20+0x30a], np.uint8).reshape(256, 3)
for i in range(0, 256, 4):
    print(i, pal[i:i+4].tolist())
