import sys, numpy as np
from PIL import Image
FR = 64000 + 768
d = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
shots = sys.argv[1]
for t in [int(x) for x in sys.argv[2:]]:
    f = np.asarray(d[t*FR:(t+1)*FR])
    idx = f[:64000].reshape(200, 320)
    pal = f[64000:].reshape(256, 3).astype(np.float32) / 63
    cap = pal[idx].mean(axis=2)
    im = Image.open('%s/shot_%05d.png' % (shots, t)).convert('RGB').resize((320, 200), Image.BOX)
    rem = np.asarray(im, dtype=np.float32).mean(axis=2) / 255
    sh = (idx >= 24) & (idx <= 31)
    # erode the shadow mask by 1 px
    e = sh.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            e &= np.roll(np.roll(sh, dy, 0), dx, 1)
    paper = (idx >= 46) & (idx <= 58)
    pe = paper.copy()
    for dy in (-2, 0, 2):
        for dx in (-2, 0, 2):
            pe &= np.roll(np.roll(paper, dy, 0), dx, 1)
    print('%d shadow px %4d cap %.4f rem %.4f | paper px %5d cap %.4f rem %.4f (x63: %.2f %.2f / %.2f %.2f)' % (
        t, e.sum(), cap[e].mean() if e.any() else 0, rem[e].mean() if e.any() else 0, pe.sum(), cap[pe].mean(), rem[pe].mean(),
        63 * cap[e].mean() if e.any() else 0, 63 * rem[e].mean() if e.any() else 0, 63 * cap[pe].mean(), 63 * rem[pe].mean()))
