import numpy as np, glob
from PIL import Image
data = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r'); FR = 64768
ovf = [(6575, 6575), (6603, 6623), (7071, 7079), (7096, 7113), (7404, 7443), (7464, 7513), (7519, 7544), (7548, 7592), (7604, 7644), (7659, 7673), (7685, 7708)]
o, r, inv = [], [], []
for f in sorted(glob.glob('s8/shot_*.png')):
    t = int(f.split('_')[1][:5]) + 1
    f0 = np.asarray(data[t * FR:(t + 1) * FR]); idx = f0[:64000].reshape(200, 320)
    m = idx < 64
    if m.sum() < 60: continue
    # interior plane pixels (avoid outlines): index >= 30 in the grey ramp
    ov = idx[m].astype(float)
    im = np.asarray(Image.open(f).convert('L')).astype(float)
    me = np.asarray(Image.open(f).convert('RGB')).astype(int)
    mm = (np.abs(me[..., 0] - me[..., 1]) < 12) & (np.abs(me[..., 1] - me[..., 2]) < 12) & (me[..., 0] > 30)
    if mm.sum() < 30: continue
    o.append(ov.mean() * 255 / 63); r.append(im[mm].mean()); inv.append(any(a <= t - 1 <= b for a, b in ovf))
o, r, inv = map(np.array, (o, r, inv))
print('n', len(o), 'corr %.3f' % np.corrcoef(o, r)[0, 1])
print('overflow ticks: orig mean %.1f remake %.1f;  normal ticks: orig %.1f remake %.1f' % (o[inv].mean(), r[inv].mean(), o[~inv].mean(), r[~inv].mean()))
