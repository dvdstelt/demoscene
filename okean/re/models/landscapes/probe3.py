import copy
import numpy as np
from common import *
import part3
from framemap import framemap
cap = Capture(); T = read_ticks_full(); IRQ0 = 2325
FM = framemap('3554b', 0, 2334, 3470, IRQ0)
cb_frame = {}
for k in sorted(T):
    n = T[k]['irqs'] - IRQ0
    if 2331 <= k <= 3470 and n > 0 and n not in cb_frame: cb_frame[n] = k
pos = lambda n: (T[cb_frame[n]]['order'], T[cb_frame[n]]['row']) if n in cb_frame else (0, 0)
st = part3.State(pos); R = part3.Renderer()
for j, k, n in FM[:915]:
    while st.n < n: st.callback()
    R.draw(st)
j, k, n = FM[915]
c = cap.idx(k)
for nn in (933, 934, 935, 936, 937):
    s2 = part3.State(pos)
    while s2.n < nn: s2.callback()
    for mix in ('all', 'scroll_prev'):
        R2 = copy.deepcopy(R)
        s3 = copy.copy(s2)
        if mix == 'scroll_prev':
            s1 = part3.State(pos)
            while s1.n < nn - 1: s1.callback()
            s3.scroll, s3.slide, s3.row = s1.scroll, s1.slide, s1.row
        img, pal = R2.draw(s3)
        print(nn, mix, np.count_nonzero(img != c))
R2 = copy.deepcopy(R)
s2 = part3.State(pos)
while s2.n < 935: s2.callback()
img, pal = R2.draw(s2)
d = img != c
ys, xs = np.nonzero(d)
print('bbox', ys.min(), ys.max(), xs.min(), xs.max())
print('model', img[ys.min():ys.min()+3, xs.min():xs.min()+12])
print('cap  ', c[ys.min():ys.min()+3, xs.min():xs.min()+12])
from PIL import Image
pl = (cap.pal(k).astype(int) * 255 // 63).astype(np.uint8)
Image.fromarray(np.concatenate([pl[img], pl[c], (d[..., None] * 255).repeat(3, 2).astype(np.uint8)], 1)).resize((1920, 400)).save('probe3.png')
