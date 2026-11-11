import numpy as np, sys
from PIL import Image
data = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r'); FR = 64768
for t in (6300, 6538, 6541, 6544, 6640, 6700, 6800, 7000, 7200, 7300, 7500, 7600):
    f = np.asarray(data[t * FR:(t + 1) * FR]); idx = f[:64000].reshape(200, 320)
    pal = f[64000:].reshape(256, 3).astype(int) * 255 // 63
    cap = pal[idx]; plane = idx < 64
    out = []
    for dt in (-1, 0, 1):
        me = np.asarray(Image.open('s6/shot_%05d.png' % (t + dt)).convert('RGB')).astype(int)
        m = (np.abs(me[..., 0] - me[..., 1]) < 12) & (np.abs(me[..., 1] - me[..., 2]) < 12) & (me[..., 0] > 30)
        can = ~plane & ~m
        eq = (np.abs(me - cap).max(-1) <= 2)[can].mean()
        iou = (m & plane).sum() / max(1, (m | plane).sum())
        out.append('dt%+d canyon %.2f plane %.2f' % (dt, eq, iou))
    print(t, ' | '.join(out))
