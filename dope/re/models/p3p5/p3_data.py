"""p3 static data: palettes, maps, picture, label, mesh -> PNG previews and stats (models/p3p5/out/)."""
import os, sys, struct
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
img = open(os.path.join(RE, 'unpacked', 'p3.bin'), 'rb').read()
OUT = os.path.join(HERE, 'out'); os.makedirs(OUT, exist_ok=True)

def so(s, o): return s * 16 + o
def pal_at(x, n): return np.frombuffer(img[x:x + 3 * n], np.uint8).reshape(n, 3)

pal = np.zeros((256, 3), np.uint8)
pal[0:240] = pal_at(so(0x306, 0x10), 240)
pal[240:256] = pal_at(so(0x3342, 0x2da), 16)
pal[0:16] = pal_at(so(0x4b81, 0), 16)
def rgb(p): return ((p.astype(np.uint16) * 255 + 31) // 63).astype(np.uint8)
np.save(os.path.join(OUT, 'p3_pal.npy'), pal)
print('palette max', pal.max())
for i in range(0, 256, 16):
    print('%3d' % i, ' '.join('%02x%02x%02x' % tuple(c) for c in pal[i:i + 16]))

def arr(x, w, h, stride=None):
    stride = stride or w
    a = np.frombuffer(img[x:x + stride * h].ljust(stride * h, b'\0'), np.uint8).reshape(h, stride)[:, :w]
    return a

def save(a, name, p=None):
    if p is None:
        Image.fromarray(a).save(os.path.join(OUT, name))
    else:
        Image.fromarray(rgb(p)[a]).save(os.path.join(OUT, name))

phong = arr(so(0x337, 0), 256, 256)
save(phong, 'p3_phongmap_idx.png'); save(phong, 'p3_phongmap.png', pal)
print('phong range', phong.min(), phong.max(), np.bincount(phong.ravel(), minlength=256).nonzero()[0][[0, -1]])
bump = arr(so(0x2341, 0), 256, 256)
save(bump, 'p3_bump_idx.png')
print('bump range', bump.min(), bump.max())
pic = arr(so(0x4bb1, 0), 320, 200)
save(pic, 'p3_picture.png', pal)
print('pic range', pic.min(), pic.max(), 'colours used', len(np.unique(pic)))
lab = arr(so(0x3342, 0x30a), 70, 44)
save(lab, 'p3_label.png', pal)
print('label range', lab.min(), lab.max(), np.unique(lab))
