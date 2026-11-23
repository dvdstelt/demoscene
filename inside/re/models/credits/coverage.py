"""Which pixels the 3D render (0x41127c + 0x40c6ac) leaves untouched: two runs that fill the draw target with
different indices before the render (run_inside.py --mark 7 / --mark 200, --dump-buf every:10); a pixel that keeps
the marker in both runs is not drawn. usage: python3 coverage.py mk7.pkl mk200.pkl"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from dumpread import records


def bufs(path, mark):
    return {r['n']: (np.frombuffer(r['buf'], np.uint8).reshape(200, 320) == mark, r['refresh'])
            for r in records(path) if r['tag'] == 'buf3d'}


a, b = bufs(sys.argv[1], 7), bufs(sys.argv[2], 200)
tot = 0
for n in sorted(set(a) & set(b)):
    und = a[n][0] & b[n][0]
    tot += und.sum()
    if und.any():
        ys, xs = np.nonzero(und)
        print('frame %d (refresh %d): %d pixels not drawn, rows %d..%d cols %d..%d' % (
            n, a[n][1], und.sum(), ys.min(), ys.max(), xs.min(), xs.max()))
print('frames checked', len(set(a) & set(b)), 'undrawn pixels in total', tot)
