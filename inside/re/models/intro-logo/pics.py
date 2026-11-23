"""Write the intro's and logo's pictures from INSIDE.DAT as PNGs (scratch output, not for Git)."""
import os, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'out')
os.makedirs(out, exist_ok=True)

def pal(name):
    p = np.frombuffer(d.get(name), np.uint8).reshape(256, 3).astype(np.uint32)
    return ((p * 255 + 31) // 63).astype(np.uint8)

def save(name, w, h, palname):
    px = np.frombuffer(d.get(name), np.uint8)[:w * h].reshape(h, w)
    Image.fromarray(pal(palname)[px]).save(os.path.join(out, os.path.basename(name) + '.png'))
    print(name, w, h, 'min', px.min(), 'max', px.max(), 'zeros', int((px == 0).sum()), 'distinct', len(np.unique(px)))
    return px

save('intro/intro.raw', 320, 200, 'intro/intro.pal')
save('logo/twlogo.raw', 256, 256, 'logo/logo.pal')
save('logo/inside.raw', 249, 139, 'logo/logo.pal')
m = save('logo/inmask.raw', 253, 147, 'logo/logo.pal')
Image.fromarray((m.astype(np.uint32) * 17).clip(0, 255).astype(np.uint8)).save(os.path.join(out, 'inmask_levels.png'))
# palettes as swatches
for p in ('intro/intro.pal', 'logo/logo.pal'):
    Image.fromarray(np.repeat(np.repeat(pal(p).reshape(16, 16, 3), 16, 0), 16, 1)).save(os.path.join(out, os.path.basename(p) + '.png'))
# shade tables as images (rows = level)
for s, n in (('intro/intro.shd', 64), ('logo/logo.shd', 64), ('logo/inmask.shd', 16)):
    t = np.frombuffer(d.get(s), np.uint8).reshape(n, 256)
    pp = pal('intro/intro.pal' if s.startswith('intro') else 'logo/logo.pal')
    Image.fromarray(np.repeat(np.repeat(pp[t], 4, 0), 2, 1)).save(os.path.join(out, os.path.basename(s) + '.png'))
