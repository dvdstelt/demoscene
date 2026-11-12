"""Where file 27 departs from the cloud render at the best offsets (bee_bg_refine.py): image of the mismatches."""
import numpy as np
from PIL import Image
from common import out, file
import bee_bg_fft as B

TEXi = np.frombuffer(file(30), np.uint8)[:65536].astype(np.int64)
T = B.T
p = np.arange(64000)
si = 86 * 256 + 108; di = 218 * 256 + 228
c1 = TEXi[(T + si) & 0xffff]
c2 = TEXi[(((T[p | 1] & 0xfffe) + di) + (p & 1)) & 0xffff]
img = (c1 + 16 * c2).reshape(200, 320)
f27 = B.f27
key = img * 256 + f27
cnt = np.bincount(key.ravel(), minlength=65536).reshape(256, 256)
mode = cnt.argmax(1)
bad = (mode[img] != f27) & (f27 != 0)
bad[0] = bad[198:] = False
print('mismatch', int(bad.sum()))
rows, cols = np.nonzero(bad)
print('rows hist', np.histogram(rows, bins=10, range=(0, 200))[0])
print('cols hist', np.histogram(cols, bins=10, range=(0, 320))[0])
# parity of columns
print('even/odd cols', int((cols % 2 == 0).sum()), int((cols % 2 == 1).sum()))
img_out = np.zeros((200, 320, 3), np.uint8)
img_out[..., 1] = (B.p26[f27][..., 1] * 3).clip(0, 255).astype(np.uint8)
img_out[bad] = (255, 0, 0)
Image.fromarray(img_out).resize((960, 600), Image.NEAREST).save(out('bee_bg_mismatch.png'))
# how far apart in rgb are the mismatching palette entries?
d = np.abs(B.p26[mode[img]] - B.p26[f27]).sum(2)
print('rgb distance of mismatches: mean %.2f max %d' % (d[bad].mean(), d[bad].max()))
