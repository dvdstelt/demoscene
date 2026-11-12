"""p5 static data: palette, env/phong maps, bump texture, labels -> PNG previews and stats (models/p3p5/out/)."""
import os
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
img = open(os.path.join(RE, 'unpacked', 'p5.bin'), 'rb').read()
OUT = os.path.join(HERE, 'out'); os.makedirs(OUT, exist_ok=True)
def so(s, o): return s * 16 + o
def pal_at(x, n): return np.frombuffer(img[x:x + 3 * n], np.uint8).reshape(n, 3)
def rgb(p): return ((p.astype(np.uint16) * 255 + 31) // 63).astype(np.uint8)
def arr(x, w, h, stride=None):
    stride = stride or w
    return np.frombuffer(img[x:x + stride * h].ljust(stride * h, b'\0'), np.uint8).reshape(h, stride)[:, :w]
pal = np.zeros((256, 3), np.uint8)
pal[16:240] = pal_at(so(0x3a6, 0x40), 224)
pal[240:256] = pal_at(so(0x248f, 0x2da), 16)
np.save(os.path.join(OUT, 'p5_pal.npy'), pal)
for i in range(0, 256, 16):
    print('%3d' % i, ' '.join('%02x%02x%02x' % tuple(c) for c in pal[i:i + 16]))
print('label pal 258a == 248f:', img[so(0x258a, 0x2da):so(0x258a, 0x30a)] == img[so(0x248f, 0x2da):so(0x248f, 0x30a)])
env = arr(so(0x3d7, 0), 256, 256)
Image.fromarray(env).save(os.path.join(OUT, 'p5_envmap_idx.png'))
Image.fromarray(rgb(pal)[env]).save(os.path.join(OUT, 'p5_envmap.png'))
for qy in (0, 128):
    for qx in (0, 128):
        q = env[qy:qy + 128, qx:qx + 128]
        print('env quadrant', qy, qx, 'range', q.min(), q.max(), 'unique', len(np.unique(q)))
bump = arr(so(0x1c96, 0), 256, 256)
Image.fromarray(bump).save(os.path.join(OUT, 'p5_bump_idx.png'))
print('bump q0 range', bump[:128, :128].min(), bump[:128, :128].max())
for seg, name in ((0x248f, 'env'), (0x258a, 'bump')):
    lab = arr(so(seg, 0x30a), 70, 44)
    Image.fromarray(rgb(pal)[lab]).resize((140, 88), Image.NEAREST).save(os.path.join(OUT, 'p5_label_%s.png' % name))
    print('label', name, np.unique(lab))
