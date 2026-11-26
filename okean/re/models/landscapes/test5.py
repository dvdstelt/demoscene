import sys
import numpy as np
from common import *
import part5
from framemap import framemap

cap = Capture(); T = read_ticks_full()
IRQ0 = T[4693]['irqs']          # timer interrupts counted up to the frame in which main calls the part
K0, K1 = 4693, 5751
M = part5.M
# the first drawn frame: the noise seed continues from the generator in the set-up 0x38469
FM = framemap('38520', M.u32(0x37130), K0, K1, IRQ0, cap)
print('first rows of the frame map', FM[:4], 'drawn frames', len(FM))
cb_frame = {}
for k in sorted(T):
    n = T[k]['irqs'] - IRQ0
    if K0 <= k <= K1 and n > 0 and n not in cb_frame:
        cb_frame[n] = k
pos = lambda n: (T[cb_frame[n]]['order'], T[cb_frame[n]]['row']) if n in cb_frame else (15, 13)
j0, j1 = int(sys.argv[1]), int(sys.argv[2])
st = part5.State(pos); R = part5.Renderer()
bad = 0
for j, k, n in FM:
    if j > j1:
        break
    while st.n < n:
        st.callback()
    tp = (T[k - 1]['order'], T[k - 1]['row'])
    img = R.draw(st, tp)
    if j >= j0:
        c = cap.idx(k)
        d = np.count_nonzero(img != c)
        # the palette written after the retrace of frame k uses the fade after that retrace's callback
        s2 = st.fade
        if d or j % 50 == 0:
            print('drawn', j, 'frame', k, 'n', n, 'diff px', d, 'fade', st.fade, flush=True)
            if d and '-v' in sys.argv:
                ys, xs = np.nonzero(img != c)
                print('   bbox', ys.min(), ys.max(), xs.min(), xs.max(), list(zip(ys[:6], xs[:6])), img[ys[:6], xs[:6]], c[ys[:6], xs[:6]])
        bad += d > 0
print('frames with differences:', bad)
