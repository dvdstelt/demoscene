import sys, time
import numpy as np
from common import *
import part3
from framemap import framemap

cap = Capture()
T = read_ticks_full()
IRQ0 = 2325
FM = framemap('3554b', Mem(load_snap('part_3554b')).u32(0x2f224), 2334, 3470, IRQ0, cap)
cb_frame = {}
for k in sorted(T):
    n = T[k]['irqs'] - IRQ0
    if 2331 <= k <= 3470 and n > 0 and n not in cb_frame:
        cb_frame[n] = k
def pos(n):
    k = cb_frame.get(n)
    return (T[k]['order'], T[k]['row']) if k else (0, 0)

j0, j1 = int(sys.argv[1]), int(sys.argv[2])
st = part3.State(pos)
R = part3.Renderer()
bad = 0
for j, k, n in FM:
    if j > j1:
        break
    while st.n < n:
        st.callback()
    img, pal = R.draw(st)
    if j >= j0:
        c = cap.idx(k)
        d = np.count_nonzero(img != c)
        dp = np.count_nonzero(pal != cap.pal(k).reshape(-1))
        if d or dp or j % 50 == 0:
            print('drawn', j, 'frame', k, 'n', n, 'diff px', d, 'pal diff', dp, 'w', st.w1, st.w2, flush=True)
        bad += d > 0
print('frames with differences:', bad)
