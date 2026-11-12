"""Value ranges of the environment maps and textures each part's segment tables point at (from a 'mid' dump), and
whether they are in the unpacked image (data) or generated at run time.
usage: python3 models/engine/maps.py <part> <dump.npz>"""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpview import Dump
from engine_model import PARTS
part, path = sys.argv[1], sys.argv[2]
d = Dump(path, part)
c = PARTS[part]
img = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'unpacked', part + '.bin'), 'rb').read()
for kind, tab in (('env', c['segenv']), ('tex', c['segtex'])):
    for i in range(4):
        s = d.cs16(tab + 2 * i)
        if not s:
            print('%s[%d] = 0' % (kind, i))
            continue
        s = d.seg_of(s)
        m = np.frombuffer(d.b(s, 0, 0x8000), np.uint8).reshape(128, 256)[:, :128]
        a = s * 16
        inimg = img[a:a + 0x8000] if a < len(img) else b''
        same = len(inimg) == 0x8000 and np.array_equal(np.frombuffer(inimg, np.uint8).reshape(128, 256)[:, :128], m)
        u = np.unique(m)
        print('%s[%d] = %04x (file 0x%05x): 128x128 window values %d..%d (%d distinct)%s' % (
            kind, i, s, a, m.min(), m.max(), len(u), ', same as the image' if same else ', differs from the image (generated / changed at run time)'))
