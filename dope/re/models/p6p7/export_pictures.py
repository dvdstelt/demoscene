"""Write the pictures p6 and p7 use as indexed PNGs (6-bit VGA palette scaled to 8 bit) into dope/re/dump/pictures/.
Each RIX3 file in the image: 'RIX3', u16 width, u16 height, 0xAF, 0x00, 768-byte palette, width*height pixels."""
import os
import struct
import numpy as np
from PIL import Image
from snap import img, RE

OUT = os.path.join(RE, 'dump', 'pictures')
os.makedirs(OUT, exist_ok=True)


def rix(b, pix_off):
    """the RIX3 file whose pixels start at pix_off: (w, h, palette 256x3 6-bit, pixels)"""
    h0 = pix_off - 768 - 10
    assert b[h0:h0 + 4] == b'RIX3', b[h0:h0 + 4]
    w, h = struct.unpack_from('<HH', b, h0 + 4)
    pal = np.frombuffer(b, np.uint8, 768, h0 + 10).reshape(256, 3)
    return w, h, pal, np.frombuffer(b, np.uint8, w * h, pix_off).reshape(h, w)


def save(name, pix, pal):
    im = Image.fromarray(np.ascontiguousarray(pix), 'P')
    p = (np.asarray(pal, np.uint16) * 255 + 31) // 63
    im.putpalette(p.astype(np.uint8).ravel().tolist())
    im.save(os.path.join(OUT, name))
    print('wrote', name, pix.shape[1], 'x', pix.shape[0])


b6, b7 = img('p6'), img('p7')
# p6
w, h, pal, pix = rix(b6, 0x0c97 * 16)
save('p6-swirl.png', pix, pal)                       # colours 0..15 only
w, h, pal1, env = rix(b6, 0x0449 * 16)
save('p6-envmap.png', env, pal1)                     # left: rings (16..79), right: cat light gradient (80..143)
pal2 = np.frombuffer(b6, np.uint8, 768, 0x100 + 0x3565).reshape(256, 3)
save('p6-envmap-metal.png', env, pal2)               # same pixels with the metal palette (cs:3565)
for name, seg in (('shadow', 0x3399), ('morph', 0x3494), ('metal', 0x358f)):
    w, h, pal, pix = rix(b6, seg * 16 + 0x30a)
    save('p6-label-%s.png' % name, pix, pal)
# p7
w, h, pal, pix = rix(b7, 0x5ab4 * 16)
save('p7-horizon.png', pix, pal)
w, h, pal, pix = rix(b7, 0x6a86 * 16)
save('p7-dope.png', pix, pal)
for name, seg in (('light', 0x3beb), ('world', 0x3ce6)):
    w, h, pal, pix = rix(b7, seg * 16 + 0x30a)
    save('p7-label-%s.png' % name, pix, pal)
w, h, palg, pix = rix(b7, 0x03b3 * 16)
save('p7-reflection-photo.png', pix, palg)           # its own grey palette (the demo shows it through p7's DAC: dark)
