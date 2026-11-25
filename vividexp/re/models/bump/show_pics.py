"""Contact sheet of the bump pictures: height pictures as grey indices and with their own palettes, light pictures
with their own palettes and the palettes the effect uses. usage: python3 show_pics.py runs/pics.png"""
import sys

import numpy as np
from PIL import Image

import bump


def pal_img(px, pal):
    lut = (pal.astype(np.uint32) * 255 + 31) // 63
    return Image.fromarray(lut[px].astype(np.uint8))


tiles = []
for n in ['bump/bumppic1.pcx', 'bump/bumppic2.pcx', 'bump/bumppic3.pcx']:
    px, pal, _ = bump.pcx(n)
    print(n, px.shape, 'index range', px.min(), px.max(), 'distinct', len(np.unique(px)))
    g = px.astype(np.float32)
    tiles.append(Image.fromarray((255 * (g - g.min()) / max(1, g.max() - g.min())).astype(np.uint8)).convert('RGB'))
    tiles.append(pal_img(px, pal).resize((320, 200)))
for n, p in [('bump/phong.pcx', 'pal/phong.pal'), ('bump/phong2.pcx', None), ('bump/phong3.pcx', None)]:
    px, pal, _ = bump.pcx(n)
    print(n, px.shape, 'index range', px.min(), px.max(), 'distinct', len(np.unique(px)))
    if p:
        pal = np.frombuffer(bump.load(p), np.uint8).reshape(256, 3)
    im = pal_img(px, pal)
    tiles.append(im.resize((320, 200)))
    tiles.append(pal_img(px, np.stack([np.arange(256) // 4] * 3, 1).astype(np.uint8)).resize((320, 200)))
W = 640
sheet = Image.new('RGB', (W, 200 * ((len(tiles) + 1) // 2)))
for i, t in enumerate(tiles):
    sheet.paste(t, ((i % 2) * 320, (i // 2) * 200))
sheet.save(sys.argv[1])
a = bump.load('pal/phong.pal'); b = bump.load('bump/phong.pal')
print('pal/phong.pal == bump/phong.pal', a == b)
