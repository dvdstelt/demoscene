"""Frames shown per refresh per frame routine in full.ticks ([0x42488c] frames copied to the screen)."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
sys.path.insert(0, RE)
from sheet import read_ticks
T = read_ticks(os.path.join(RE, 'full.ticks'))
ks = sorted(T)
seg = []
for k in ks:
    fn = T[k].frame_fn
    if not seg or seg[-1][0] != fn:
        seg.append([fn, k, k])
    seg[-1][2] = k
for fn, a, b in seg:
    n = b - a + 1
    shown = T[b].shown - T[a - 1].shown if a > 0 else T[b].shown
    print('frame routine %6x records %5d..%5d (%5d refreshes): %5d frames shown, %.3f per refresh' % (
        fn, a, b, n, shown, shown / n))
