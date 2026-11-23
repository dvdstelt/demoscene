"""Look at the temple's tables: temple.pal, temple.int, flare.shd, temple.raw, flare.raw."""
import sys, os
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import datfile
from PIL import Image
d = datfile.Dat(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'original', 'INSIDE.DAT'))
pal = np.frombuffer(d.get('temple/temple.pal'), np.uint8).reshape(256, 3).astype(int)
tint = np.frombuffer(d.get('temple/temple.int'), np.uint8).reshape(256, 256)
shd = np.frombuffer(d.get('temple/flare.shd'), np.uint8).reshape(256, 256)
tex = np.frombuffer(d.get('temple/temple.raw'), np.uint8).reshape(256, 256)
fl = np.frombuffer(d.get('temple/flare.raw'), np.uint8)
lum = pal @ np.array([0.299, 0.587, 0.114])
if __name__ == '__main__':
    np.set_printoptions(linewidth=200)
    print('pal max', pal.max(), 'first 16', pal[:16].tolist())
    print('pal 0..255 every 8:'); print(pal[::8].tolist())
    print('lum by index (every 8):', np.round(lum[::8], 1).tolist())
    print('tex index range', tex.min(), tex.max(), 'histogram top', np.bincount(tex.ravel(), minlength=256).argsort()[-10:])
    print('flare.raw range', fl.min(), fl.max(), 'size', fl.size)
    print('int rows 0, 64, 128, 192, 255 (every 16 cols):')
    for r in (0, 32, 64, 128, 192, 255): print(r, tint[r, ::16].tolist())
    print('int col 0, 100 (every 16 rows):', tint[::16, 0].tolist(), tint[::16, 100].tolist())
    print('shd rows (every 16 cols):')
    for r in (0, 16, 32, 48, 64, 128, 192, 255): print(r, shd[r, ::16].tolist())
    # is shd[r][c] a "brighter" colour of c as r grows?
    for r in (0, 32, 64, 128, 255):
        print('shd row', r, 'mean lum', round(lum[shd[r]].mean(), 2), 'identity frac', (shd[r] == np.arange(256)).mean())
    for r in (0, 64, 128, 192, 255):
        print('int row', r, 'mean lum', round(lum[tint[r]].mean(), 2), 'identity frac', (tint[r] == np.arange(256)).mean(),
              'col-const frac', (tint[r] == tint[r, 0]).mean())
    # flare.raw as an image
    for w in (128, 104):
        Image.fromarray(fl.reshape(-1, w).astype(np.uint8)).save(f'out/flare_{w}.png')
    Image.fromarray((pal[tex] * 255 // 63).astype(np.uint8)).save('out/temple_tex.png')
