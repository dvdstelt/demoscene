"""Write p2's hand-made pictures as PNG files with their palettes into dope/re/dump/pictures/ (git-ignored):

p2-credits.png     RIX3 320x128 at 0451:0000 (pixels 0451:030a), shown with the DAC palette p2 sets
p2-label-blur.png  RIX3 70x44 at 2bd2:0000 (pixels 2bd2:030a), its own palette (colours 240..255)
p2-background.png  raw 320x200 at 0ec3:0000, DAC palette (the "stand" backdrop)
and the generated maps into models/p2/out/ only:
p2-envmap.png      RIX3 256x128 at 6507:0006 (pixels 6538:0000): two 128x128 env (highlight) maps, DAC palette
p2-bumpmap.png     raw 128x128 (stride 256) at 23d1:0000: grey = the byte; p2-bumpmap-dxdy.png: decoded offsets
p2-dac.png         the 256-colour DAC palette as a 16x16 swatch
models/p2/out/ also gets 3x copies of everything for viewing.
"""
import os, struct
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
img = open(os.path.join(HERE, '../../unpacked/p2.bin'), 'rb').read()
OUT = os.path.join(HERE, '../../dump/pictures')
VIEW = os.path.join(HERE, 'out')
os.makedirs(OUT, exist_ok=True); os.makedirs(VIEW, exist_ok=True)

def rix(off):
    assert img[off:off+4] == b'RIX3'
    w, h = struct.unpack_from('<HH', img, off + 4)
    pal = np.frombuffer(img[off+10:off+10+768], np.uint8).reshape(256, 3)
    pix = np.frombuffer(img[off+0x30a:off+0x30a+w*h], np.uint8).reshape(h, w)
    return pal, pix

def dac():
    """The DAC palette p2 sets at 377f: 240 colours from 6507:0010, 16 from 2bd2:02da."""
    return np.frombuffer(img[0x65080:0x65080+720] + img[0x2bd20+0x2da:0x2bd20+0x30a], np.uint8).reshape(256, 3)

def save(name, pix, pal6, hand=False):
    pal8 = ((pal6.astype(np.uint16) * 255 + 31) // 63).astype(np.uint8)
    im = Image.fromarray(pix, 'P'); im.putpalette(pal8.flatten().tolist())
    if hand:
        im.save(os.path.join(OUT, name))
    im.convert('RGB').resize((pix.shape[1]*3, pix.shape[0]*3), Image.NEAREST).save(os.path.join(VIEW, name))

if __name__ == '__main__':
    D = dac()
    pal, pix = rix(0x4510); save('p2-credits.png', pix, D, True)
    print('credits own palette 128..190 == DAC?', (pal[128:191] == D[128:191]).all())
    pal, pix = rix(0x2bd20); save('p2-label-blur.png', pix, pal, True)
    pal, pix = rix(0x65076); save('p2-envmap.png', pix, D)
    print('envmap own palette 0..239 == DAC?', (pal[:240] == D[:240]).all())
    bg = np.frombuffer(img[0xec30:0xec30+64000], np.uint8).reshape(200, 320); save('p2-background.png', bg, D, True)
    bm = np.frombuffer(img[0x23d10:0x23d10+0x8000], np.uint8).reshape(128, 256)[:, :128].copy()
    grey = np.array([[i*63//255]*3 for i in range(256)], np.uint8)
    save('p2-bumpmap.png', bm, grey)
    tab = np.array(struct.unpack_from('<256h', img, 0x100))
    dy = (tab + 0x80) >> 8; dx = tab - dy * 256
    print('offset table: dx by (k>>4):', [int(dx[k*16]) for k in range(16)], ' dy by (k&15):', [int(dy[k]) for k in range(16)])
    ok = all(dx[k] == dx[(k >> 4) * 16] and dy[k] == dy[k & 15] for k in range(256))
    print('table = dy(k&15)*256 + dx(k>>4) for all k:', ok)
    rgb = np.zeros((128, 128, 3), np.uint8)
    rgb[..., 0] = np.clip(128 + dx[bm] * 18, 0, 255); rgb[..., 1] = np.clip(128 + dy[bm] * 18, 0, 255)
    Image.fromarray(rgb).resize((384, 384), Image.NEAREST).save(os.path.join(VIEW, 'p2-bumpmap-dxdy.png'))
    sw = np.arange(256, dtype=np.uint8).reshape(16, 16).repeat(8, 0).repeat(8, 1); save('p2-dac.png', sw, D)
