import numpy as np, sys
from PIL import Image
sys.path.insert(0, '../re-planes/py')
from snap import M
RE = '/home/dennis/Downloads/acme_303/paper/re/'
data = np.memmap(RE + 'full.frames', dtype=np.uint8, mode='r'); FR = 64768
for snap, shot, rec in ((7000, 'ex/shot_06999.png', 6999), (9500, 'ex/shot_09491.25.png', 9499)):
    m = M(snap)
    pal = np.frombuffer(m.s['pal'], np.uint8).reshape(256, 3).astype(int)
    img = np.load('../re-planes/py/img_%d.npy' % snap).reshape(200, 320)   # canyon only (block renderer)
    cap = np.asarray(data[rec * FR:rec * FR + 64000]).reshape(200, 320)
    me = np.asarray(Image.open(shot).convert('RGB')).astype(int)
    # nearest canyon ramp entry
    ramp = pal[128:160] * 255 // 63
    d = ((me[:, :, None, :] - ramp[None, None]) ** 2).sum(-1)
    idx = 128 + d.argmin(-1)
    plane = cap < 64
    sel = ~plane
    eq = (idx == img)[sel].mean(); near = (np.abs(idx - img) <= 1)[sel].mean()
    print(snap, 'canyon pixels equal %.4f within 1 step %.4f' % (eq, near), 'mean idx diff %.3f' % (idx - img)[sel].mean())
    # rows where they differ
    diff = (idx != img) & sel
    print('  diff rows hist', np.bincount(np.nonzero(diff)[0] // 20, minlength=10))
    vis = np.concatenate([pal[img] * 4, me, np.repeat((diff * 255)[..., None], 3, 2)], 1).clip(0, 255).astype(np.uint8)
    Image.fromarray(vis).resize((1920, 400), Image.NEAREST).save('exchk_%d.png' % snap)
