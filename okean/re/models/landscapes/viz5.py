import sys
import numpy as np
from PIL import Image
from common import *
import part5
from framemap import framemap
cap = Capture(); T = read_ticks_full()
IRQ0 = T[4693]['irqs']          # timer interrupts counted up to the frame in which main calls the part
FM = framemap('38520', 0, 4693, 5751, IRQ0)
print(FM[:5])
cb_frame = {}
for k in sorted(T):
    n = T[k]['irqs'] - IRQ0
    if 4693 <= k <= 5751 and n > 0 and n not in cb_frame: cb_frame[n] = k
pos = lambda n: (T[cb_frame[n]]['order'], T[cb_frame[n]]['row']) if n in cb_frame else (15, 13)
J = int(sys.argv[1])
st = part5.State(pos); R = part5.Renderer()
for j, k, n in FM[:J + 1]:
    while st.n < n: st.callback()
    img = R.draw(st, (T[k - 1]['order'], T[k - 1]['row']))
c = cap.idx(k)
def show(a):
    a = np.minimum(a.astype(int) * 4, 255).astype(np.uint8)
    return a
d = (img.astype(int) - c.astype(int))
dd = np.where(d == 0, 128, np.where(d > 0, 255, 0)).astype(np.uint8)
Image.fromarray(np.concatenate([show(img), show(c), dd], 1)).resize((1920, 400), Image.NEAREST).save('viz5.png')
print('frame', k, 'n', n, 'diff', np.count_nonzero(d))
