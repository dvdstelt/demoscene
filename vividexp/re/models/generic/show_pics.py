"""Save the scenes' pictures as PNG through their palette (Palette= file, 6 bit) and as raw indices / maps."""
import numpy as np
from PIL import Image
import gen

for d in ('gen', 'gen2'):
    pal = np.frombuffer(gen.load(d + '/texture.pal'), np.uint8).reshape(256, 3).astype(np.uint32)
    lut = ((pal * 255 + 31) // 63).astype(np.uint8)
    tiles = []
    names = ['envmap.pcx', 'texture.pcx', 'bump.pcx'] + (['back.pcx'] if d == 'gen2' else [])
    for n in names:
        px, ppal = gen.pcx(d + '/' + n)
        print(d, n, px.shape, 'indices', px.min(), px.max(), 'unique', len(np.unique(px)),
              'pcx palette == texture.pal*4?', bool((ppal >> 2 == pal).all()))
        img = lut[px] if n != 'bump.pcx' else np.stack([px] * 3, -1)
        Image.fromarray(img).save('runs/%s_%s.png' % (d, n[:-4]))
    # palette strip
    Image.fromarray(np.repeat(np.repeat(lut[np.arange(256).reshape(8, 32)], 12, 0), 12, 1)).save('runs/%s_pal.png' % d)
    ph = gen.phong_map(128, 0, 28672, 36864, 12)
    Image.fromarray((ph * 2).astype(np.uint8)).save('runs/%s_phong.png' % d)
