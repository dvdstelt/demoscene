"""Where the zoom model and the capture differ: rows/columns of differing pixels for some records."""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
import core_model as cm                          # noqa: E402
from sheet import open_frames, indexed           # noqa: E402

raw, n = open_frames(sys.argv[1])
want = [int(x) for x in sys.argv[3:]]
script = cm.read_script(sys.argv[2], 1)


def on_record(k, c, waiting):
    if k not in want:
        return
    px, pal = indexed(raw, k)
    m = px != c.vga.reshape(200, 320)
    ys, xs = np.nonzero(m)
    zs = waiting[2] if waiting is not None and waiting[0] == 'zoom' else None
    print(k, 'z', zs and zs['z'], 'diff', m.sum(), 'rows', ys.min() if len(ys) else None, ys.max() if len(ys) else None,
          'cols', xs.min() if len(xs) else None, xs.max() if len(xs) else None)
    if zs:
        print('   params', c.zoom_frame(zs['a'], zs['b'], zs['z']))
        for z in (zs['z'] + 3.5, zs['z'] - 3.5):
            c2 = cm.Core(); c2.slots = c.slots; c2.zbuf = np.zeros(65536, np.uint8); c2.vga = c.vga.copy()
            c2.zoom_frame(zs['a'], zs['b'], z)
            print('   with z=%g: diff %d' % (z, (px != c2.vga.reshape(200, 320)).sum()))


cm.replay(script, max(want), on_record)
