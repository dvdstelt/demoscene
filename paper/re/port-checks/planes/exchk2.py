import numpy as np, sys
from PIL import Image
sys.path.insert(0, '../re-planes/py')
from snap import M
RE = '/home/dennis/Downloads/acme_303/paper/re/'
data = np.memmap(RE + 'full.frames', dtype=np.uint8, mode='r'); FR = 64768
for snap, shot, rec in ((7000, 'ex/shot_06999.png', 6999), (9500, 'ex/shot_09491.25.png', 9499)):
    m = M(snap)
    pal = np.frombuffer(m.s['pal'], np.uint8).reshape(256, 3).astype(int)
    Mx = np.array(m.ff(0x23a8, 9)).reshape(3, 3); C = np.array(m.ff(0x23cc, 3)); H = m.f(0x23e4)
    print('snapshot cam', C, 'H', H)
    tex = np.load('../re-planes/py/tex_%d.npy' % snap).reshape(256, 256).astype(int)
    img = np.load('../re-planes/py/img_%d.npy' % snap).reshape(200, 320)
    X, Y = np.meshgrid(np.arange(320.0), np.arange(200.0))
    d = (X / 8 - 20)[..., None] * Mx[0] + (Y / 8 - 12.51)[..., None] * Mx[1] + 28.0 * Mx[2]
    t = (H - C[2]) / d[..., 2]; t = np.where(t < 0, (-H - C[2]) / d[..., 2], t)
    u = np.rint((t * d[..., 1] + C[1]) * -12.51); v = np.rint((t * d[..., 0] + C[0]) * -12.51)
    s = np.clip(8 * (1024 - np.rint(t)), 2048, 8192); k = (s // 256).astype(int)
    L = tex[(u // 256).astype(np.int64) & 255, (v // 256).astype(np.int64) & 255] - 128
    py = 128 + np.maximum(0, L - (32 - k))
    me = np.asarray(Image.open(shot).convert('RGB')).astype(int)
    ramp = pal[128:160] * 255 // 63
    idx = 128 + ((me[:, :, None, :] - ramp[None, None]) ** 2).sum(-1).argmin(-1)
    cap = np.asarray(data[rec * FR:rec * FR + 64000]).reshape(200, 320); sel = cap >= 64
    print(snap, 'remake vs python per-pixel: equal %.4f' % (idx == py)[sel].mean(), ' python per-pixel vs block renderer: equal %.4f' % (py == img)[sel].mean())
