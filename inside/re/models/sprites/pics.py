"""Dump the sprite part's pictures and tables from INSIDE.DAT as PNG (to a directory given as argument).

usage: python3 models/sprites/pics.py OUTDIR
Pictures are shown through their own palette (nigga<k>.pal; entries 0..2 of nigga2/9/4.pal are zeroed by the init
routines, as the program does). Tables (.shd, .qnt, .sdw) are written as grey images (value = index) and as images
through the palette.
"""
import os, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile

d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def pal(k, zero=True):
    p = np.frombuffer(d.get(f'nigga/nigga{k}.pal'), np.uint8).reshape(256, 3).copy()
    p = np.minimum(p, 63)
    if zero and k != 1:
        p[0] = 0
    return ((p.astype(np.uint32) * 255 + 31) // 63).astype(np.uint8)


def save(name, px, p, scale=1):
    img = Image.fromarray(p[px])
    if scale != 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    img.save(os.path.join(out, name))


sizes = {1: (164, 156), 2: (172, 200), 9: (274, 147), 4: (170, 168)}
for k in (1, 2, 9, 4):
    p = pal(k)
    bg = np.frombuffer(d.get(f'nigga/nigga{k}.raw'), np.uint8).reshape(256, 256)
    save(f'nigga{k}.png', bg, p, 2)
    w, h = sizes[k]
    sp = np.frombuffer(d.get(f'nigga/sprite{k}.raw'), np.uint8).reshape(h, w)
    save(f'sprite{k}.png', sp, p, 2)
    print(k, 'bg indices', np.unique(bg).size, bg.min(), bg.max(), 'sprite indices', sp.min(), sp.max(),
          'zero px', (sp == 0).sum())
    for ext in ('shd', 'qnt'):
        n = f'nigga/nigga{k}.{ext}'
        if n in d.files:
            t = np.frombuffer(d.get(n), np.uint8)
            t = t.reshape(-1, 256)
            Image.fromarray(t).save(os.path.join(out, f'nigga{k}_{ext}_grey.png'))
            save(f'nigga{k}_{ext}.png', t, p)
            print('  ', n, t.shape)
    if f'nigga/shadow{k}.raw' in d.files:
        s = np.frombuffer(d.get(f'nigga/shadow{k}.raw'), np.uint8)
        n = int(np.sqrt(s.size))
        s = s.reshape(n, n)
        Image.fromarray(s).save(os.path.join(out, f'shadow{k}_grey.png'))
        print('  shadow', s.shape, np.unique(s)[:40], np.unique(s).size)
        t = np.frombuffer(d.get(f'nigga/shadow{k}.sdw'), np.uint8).reshape(-1, 256)
        Image.fromarray(t).save(os.path.join(out, f'shadow{k}_sdw_grey.png'))
        save(f'shadow{k}_sdw.png', t, p, 2)
        print('  sdw', t.shape)
