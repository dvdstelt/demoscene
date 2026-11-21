"""Look at the flower part's pictures and tables: jeesus1.raw / jeesus2.raw (256x256 8-bit), jeesus.pal, jeesus.shd
(64 levels x 256 colours). Writes tex.png (both textures through the palette, and the shade table as an image)."""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sro  # noqa: E402

d = sro.dat()
pal = np.frombuffer(d.get('flower/jeesus.pal'), np.uint8).reshape(256, 3)
lut = (pal.astype(np.uint32) * 255 + 31) // 63
t1 = np.frombuffer(d.get('flower/jeesus1.raw'), np.uint8).reshape(256, 256)
t2 = np.frombuffer(d.get('flower/jeesus2.raw'), np.uint8).reshape(256, 256)
shd = np.frombuffer(d.get('flower/jeesus.shd'), np.uint8).reshape(64, 256)
print('palette max', pal.max(), 'entry 0', pal[0], 'entry 255', pal[255])
print('jeesus1 indices', t1.min(), t1.max(), 'unique', len(np.unique(t1)))
print('jeesus2 indices', t2.min(), t2.max(), 'unique', len(np.unique(t2)))
print('shd row 0 (level 0):', shd[0, :16], 'row 32:', shd[32, :16], 'row 63:', shd[63, :16])
# is level 32 (or another) the identity?
for L in range(64):
    if np.array_equal(shd[L], np.arange(256)):
        print('identity at level', L)
# brightness of shd rows: mean luminance of palette entries
lum = pal.astype(float) @ [0.3, 0.59, 0.11]
print('mean luminance per level:', ' '.join('%d:%.1f' % (L, lum[shd[L]].mean()) for L in range(0, 64, 4)))
img = np.zeros((256 + 64 * 4, 512 + 16, 3), np.uint8)
img[:256, :256] = lut[t1]
img[:256, 256 + 16:] = lut[t2]
img[256:, :256 * 2:2] = np.repeat(lut[shd], 4, axis=0)
img[256:, 1:256 * 2:2] = np.repeat(lut[shd], 4, axis=0)
Image.fromarray(img).resize((img.shape[1] * 2, img.shape[0] * 2), Image.NEAREST).save(os.path.join(HERE, 'tex.png'))
# palette strip
strip = np.repeat(np.repeat(lut[np.arange(256).reshape(16, 16)], 16, 0), 16, 1).astype(np.uint8)
Image.fromarray(strip).save(os.path.join(HERE, 'pal.png'))
