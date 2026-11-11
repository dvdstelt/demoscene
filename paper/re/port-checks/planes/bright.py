import numpy as np, sys
from PIL import Image
sys.path.insert(0, '../re-planes/py')
from snap import M
RE = '/home/dennis/Downloads/acme_303/paper/re/'
data = np.memmap(RE + 'full.frames', dtype=np.uint8, mode='r'); FR = 64768
for snap, shot, rec in ((7000, 'hq/shot_06999.png', 6999), (9500, 'hq/shot_09491.25.png', 9499)):
    m = M(snap)
    pal = np.frombuffer(m.s['pal'], np.uint8).reshape(256, 3).astype(float) * 255 / 63
    img = np.load('../re-planes/py/img_%d.npy' % snap).reshape(200, 320)
    cap = np.asarray(data[rec * FR:rec * FR + 64000]).reshape(200, 320)
    me = np.asarray(Image.open(shot).convert('RGB').resize((320, 200), Image.BOX)).astype(float)
    sel = (cap >= 64)
    sel[1:] &= sel[:-1]; sel[:, 1:] &= sel[:, :-1]
    o = pal[img][sel]; r = me[sel]
    print(snap, 'orig mean', o.mean(0).round(2), 'remake mean', r.mean(0).round(2))
    # brightness per depth band: rows of the image sorted by original brightness
    for lo, hi in ((0, 20), (20, 60), (60, 120), (120, 256)):
        s2 = (o.mean(1) >= lo) & (o.mean(1) < hi)
    # blur both to compare local means
    # local means over 16x16 blocks
    def blk(a): return a[:192, :320].reshape(12, 16, 20, 16, 3).mean((1, 3)).mean(-1)
    ob = blk(pal[img]); rb = blk(me)
    print('  16x16 block mean abs diff %.2f, corr %.3f' % (np.abs(ob - rb).mean(), np.corrcoef(ob.ravel(), rb.ravel())[0, 1]))
