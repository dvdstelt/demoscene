"""Check the palette of every captured frame of part 5 against fade(n) with n = callbacks up to that frame."""
import numpy as np
from common import *
import part5
cap = Capture(); T = read_ticks_full()
IRQ0 = T[4693]['irqs']
cb_frame = {}
for k in sorted(T):
    n = T[k]['irqs'] - IRQ0
    if 4693 <= k <= 5751 and n > 0 and n not in cb_frame: cb_frame[n] = k
pos = lambda n: (T[cb_frame[n]]['order'], T[cb_frame[n]]['row']) if n in cb_frame else (15, 13)
st = part5.State(pos)
bad = 0; first18 = None
for k in range(4693, 5752):
    n = T[k]['irqs'] - IRQ0
    while st.n < n:
        st.callback()
    if first18 is None and pos(st.n)[0] == 0x12: first18 = (k, st.n)
    p = part5.Renderer.palette(st.fade)
    c = cap.pal(k).reshape(-1)
    if k == 4693:
        print('frame 4693 palette max', c.max())
    elif np.any(p != c):
        bad += 1
        if bad < 5: print('pal mismatch frame', k, 'n', n, 'fade', st.fade)
print('palette mismatches', bad, 'first callback in order 18:', first18, 'fade at end', st.fade, 'n at end', st.n)
