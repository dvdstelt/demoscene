import numpy as np, glob
from PIL import Image
data = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r'); FR = 64768
for f in sorted(glob.glob('s5/shot_*.png')):
    t = int(f.split('_')[1][:5])
    im = np.asarray(Image.open(f).convert('RGB')).astype(int)
    m = (np.abs(im[..., 0] - im[..., 1]) < 12) & (np.abs(im[..., 1] - im[..., 2]) < 12) & (im[..., 0] > 30)
    cap = np.asarray(data[t * FR:t * FR + 64000]).reshape(200, 320) < 64
    def bb(m):
        if not m.sum(): return ''
        ys, xs = np.nonzero(m); return (xs.min(), xs.max(), ys.min(), ys.max())
    iou = (m & cap).sum() / max(1, (m | cap).sum())
    print(t, 'orig', cap.sum(), bb(cap), ' remake', m.sum(), bb(m), ' IoU %.2f' % iou)
