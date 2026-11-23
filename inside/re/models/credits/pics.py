"""Write the credits part's pictures and textures as PNG (to a scratch directory given as argv[1]) with tunnel.pal."""
import os, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
out = sys.argv[1]
pal = np.frombuffer(d.get('credits/tunnel.pal'), np.uint8).reshape(256, 3).astype(np.uint32)
lut = ((pal * 255 + 31) // 63).astype(np.uint8)
sizes = {'kredu1': (173, 75), 'kredu2': (146, 75), 'kredu3': (318, 82), 'kredu6': (294, 75), 'kredu7': (129, 75),
         'kredu8': (154, 72), 'wtrmask': (320, 200), 'piikkis': (256, 256), 'sauhu': (64, 64), 'tunnel': (256, 1024)}
for n, (w, h) in sizes.items():
    a = np.frombuffer(d.get('credits/%s.raw' % n), np.uint8)
    assert len(a) == w * h, (n, len(a))
    a = a.reshape(h, w)
    Image.fromarray(lut[a]).resize((w * 3, h * 3), Image.NEAREST).save(os.path.join(out, n + '.png'))
    vals, cnt = np.unique(a, return_counts=True)
    print(n, w, h, 'indices', len(vals), 'min', vals.min(), 'max', vals.max(), 'top', sorted(zip(cnt, vals))[-5:])
