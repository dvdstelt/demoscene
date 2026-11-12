"""Writes the hand-drawn pictures of the core scope as indexed PNGs with their palettes to dump/pictures/:
  p1-logo.png         the "Complex Media Labs" logo, the whole RIX3 picture (1024x200) at p1 00E4:0000 with its own
                      palette (= p1's palette B at 0010:07F8, red / orange; the end of the cross-fade)
  p1-logo-violet.png  the same pixels with p1's palette A at 0010:04F0 (violet; the start of the cross-fade)
  p1-logo-shown.png   the 1024x195 part that p1 copies to the VGA (rows 1..195), palette B
Also prints the two 64-colour ramps.

usage: python3 models/core/pictures.py     (from dope/re, inside the toolbox)"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(RE, 'dump', 'pictures')
img = open(os.path.join(RE, 'unpacked', 'p1.bin'), 'rb').read()

RIX = 0xe40
assert img[RIX:RIX + 4] == b'RIX3'
w, h = int.from_bytes(img[RIX + 4:RIX + 6], 'little'), int.from_bytes(img[RIX + 6:RIX + 8], 'little')
paltype, storage = img[RIX + 8], img[RIX + 9]
rixpal = np.frombuffer(img[RIX + 10:RIX + 10 + 768], np.uint8).reshape(256, 3)
pix = np.frombuffer(img[RIX + 10 + 768:RIX + 10 + 768 + w * h], np.uint8).reshape(h, w)
palA = np.frombuffer(img[0x100 + 0x4f0:0x100 + 0x4f0 + 768], np.uint8).reshape(256, 3)
palB = np.frombuffer(img[0x100 + 0x7f8:0x100 + 0x7f8 + 768], np.uint8).reshape(256, 3)
print('RIX3 %dx%d palette type %02x storage %02x; pixels %d..%d; RIX palette == palette B: %s; entries 64..255 of A/B '
      'zero: %s %s' % (w, h, paltype, storage, pix.min(), pix.max(), np.array_equal(rixpal, palB),
                       not palA[64:].any(), not palB[64:].any()))


def png(name, p, pal):
    im = Image.fromarray(p, 'P')
    im.putpalette(((pal.astype(np.uint32) * 255 + 31) // 63).astype(np.uint8).ravel().tolist())
    im.save(os.path.join(OUT, name))
    print('wrote', os.path.join('dump', 'pictures', name), p.shape[1], 'x', p.shape[0])


os.makedirs(OUT, exist_ok=True)
png('p1-logo.png', pix, rixpal)
png('p1-logo-violet.png', pix, palA)
png('p1-logo-shown.png', np.ascontiguousarray(pix[1:196]), rixpal)
for nm, pal in (('A (0010:04F0)', palA), ('B (0010:07F8)', palB)):
    print('palette', nm)
    for i in range(0, 64, 8):
        print('  ', ' '.join('%2d,%2d,%2d' % tuple(pal[j]) for j in range(i, i + 8)))
    lum = pal[:64].astype(int) @ np.array([2, 4, 1])
    print('   luminance monotonic:', bool(np.all(np.diff(lum) >= 0)), ' channels monotonic:',
          [bool(np.all(np.diff(pal[:64, c].astype(int)) >= 0)) for c in range(3)])
