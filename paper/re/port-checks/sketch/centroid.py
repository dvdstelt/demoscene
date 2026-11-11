# pencil (red body) centroid: capture vs remake shots, in VGA units
import sys, numpy as np
from PIL import Image
FR = 64000 + 768
d = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
shots = sys.argv[1]
ticks = [int(t) for t in open(shots + '/ticks.txt').read().strip().split(',')]
def cap(t):
    f = np.asarray(d[t*FR:t*FR+64000]).reshape(200, 320)
    ys, xs = np.nonzero(f >= 0xe0)
    return (xs.mean() + 0.5, ys.mean() + 0.5, len(xs)) if len(xs) else None
def rem(t):
    im = np.asarray(Image.open('%s/shot_%05d.png' % (shots, t)).convert('RGB'), dtype=np.float32) / 255
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    m = (r > 0.55) & (r - g > 0.3) & (r - b > 0.3)
    ys, xs = np.nonzero(m)
    h, w = m.shape
    return ((xs.mean() + 0.5) * 320 / w, (ys.mean() + 0.5) * 200 / h, len(xs) * 64000 / (w * h)) if len(xs) else None
for t in ticks:
    c, r = cap(t), rem(t)
    c1 = cap(t - 1)
    if c and r:
        print('%5d cap (%6.1f %6.1f) n%4d | rem (%6.1f %6.1f) n%6.0f | d %5.2f %5.2f | cap(t-1) d %5.2f %5.2f' % (
            t, c[0], c[1], c[2], r[0], r[1], r[2], r[0] - c[0], r[1] - c[1], r[0] - c1[0], r[1] - c1[1]))
    else:
        print(t, c, r)
