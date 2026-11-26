"""Write the font sheet (code32 0x3cec2, 320 x 38 bytes, indices 0 / 7 / 216 / 254) as PNGs.

font_sheet.png        the sheet as stored, through the end part's palette (code32 0x4cd64, 6-bit), x4
font_sheet_indexed.png the sheet 1:1 as an indexed PNG (the stored indices, end part's palette)
font_sheet_false.png  index 0 = dark blue, 7 = red, 216 = green, 254 = white, with the glyph boundaries, x4
font_glyphs.png       each glyph a..z cut out the way the printer cuts it (column = sum of the widths before it,
                      width w), with a 3-pixel gap, x4
usage: python3 models/core/font_sheet.py   (from okean/re)
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
img = open(os.path.join(RE, 'unpacked/korso2.bin'), 'rb').read()
B = 0xec0
A, W, H = 0x3cec2, 320, 38
sheet = np.frombuffer(img[B + A:B + A + W * H], np.uint8).reshape(H, W)
widths = list(img[B + 0x4c8c2:B + 0x4c8c2 + 26])
pal = np.frombuffer(img[B + 0x4cd64:B + 0x4cd64 + 768], np.uint8).reshape(256, 3).astype(np.uint16)
pal8 = ((pal * 255 + 31) // 63).astype(np.uint8)
for i in (0, 7, 216, 254):
    print('palette 0x4cd64 entry %3d = %s' % (i, tuple(pal[i])))
S = 4
pim = Image.fromarray(sheet, 'P')
pim.putpalette(pal8.reshape(-1).tolist())
pim.save(os.path.join(HERE, 'font_sheet_indexed.png'))
Image.fromarray(pal8[sheet]).resize((W * S, H * S), Image.NEAREST).save(os.path.join(HERE, 'font_sheet.png'))
fc = np.zeros((256, 3), np.uint8)
fc[0] = (0, 0, 60); fc[7] = (200, 40, 40); fc[216] = (40, 200, 40); fc[254] = (255, 255, 255)
im = Image.fromarray(fc[sheet]).resize((W * S, H * S), Image.NEAREST)
d = ImageDraw.Draw(im)
x = 0
starts = []
for k, w in enumerate(widths):
    starts.append(x)
    d.line([(x * S, 0), (x * S, 3)], fill=(255, 255, 0))
    d.text((x * S + 2, H * S - 12), chr(97 + k), fill=(255, 255, 0))
    x += w
im.save(os.path.join(HERE, 'font_sheet_false.png'))
print('glyph start columns', list(zip([chr(97 + k) for k in range(26)], starts, widths)))
print('last column used', starts[-1] + widths[-1] - 1)
# rows / columns used
used = np.nonzero(sheet.any(axis=0))[0]
print('nonzero columns %d..%d' % (used.min(), used.max()))
# cut-out glyphs side by side with a 3-pixel gap
tot = sum(widths) + 3 * 25
g = np.zeros((H, tot), np.uint8)
x = 0
for k, w in enumerate(widths):
    g[:, x:x + w] = sheet[:, starts[k]:starts[k] + w]
    x += w + 3
Image.fromarray(fc[g]).resize((tot * S, H * S), Image.NEAREST).save(os.path.join(HERE, 'font_glyphs.png'))
# per glyph: rows used, colours used
for k, w in enumerate(widths):
    c = sheet[:, starts[k]:starts[k] + w]
    rows = np.nonzero(c.any(axis=1))[0]
    print(chr(97 + k), 'rows %d..%d' % (rows.min(), rows.max()), 'cols used', np.nonzero(c.any(axis=0))[0].min(),
          np.nonzero(c.any(axis=0))[0].max(), 'of', w, dict(zip(*np.unique(c[c > 0], return_counts=True))))
