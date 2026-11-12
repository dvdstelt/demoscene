"""Write p4's pictures and maps as PNGs into dope/re/dump/pictures/ (made from the original: ignored by git).

  p4-label-fire-clouds.png, p4-label-fractal-scape.png  RIX3 0273:0000 / 036e:0000, 70x44, colours 240..254
  p4-nebula.png        RIX3 4eed:0006 (pixels at 4f1e:0000), 320x200, 16 colours: p3's background, the same bytes
                       as p3's copy; p4 shows p3's last screen, which equals it
  p4-chequerboard.png  IFF PBM 177a:0006, 320x200, 15 colours (0..14): p5's background, shown at p4's end
  p4-cloud.png         1ec1:0000, 256x256 bytes 0..127 (shown x2), the fire's source texture
  p4-landscape-height.png  2eec:0000, 256x256 bytes 0..118 (shown x2)
  p4-landscape-colour.png  3eec:0000, 256x256 bytes 1..12, with the landscape palette 0010:20d2
The swirl map 07d9 is written to the folder given as the first argument (default: dump/pictures) as
p4-swirlmap.png (u | v, 160x200 each), since it is generated geometry, not a picture.
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

RE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
img = open(os.path.join(RE, 'unpacked', 'p4.bin'), 'rb').read()
OUT = os.path.join(RE, 'dump', 'pictures')
SCR = sys.argv[1] if len(sys.argv) > 1 else OUT          # where the non-picture tables go
os.makedirs(OUT, exist_ok=True)


def pal6(o, n=256):
    p = np.frombuffer(img[o:o + 3 * n], np.uint8).reshape(n, 3).astype(np.int32)
    return np.clip((p * 255 + 31) // 63, 0, 255).astype(np.uint8)


def save_p(name, px, pal, d=OUT):
    im = Image.fromarray(px.astype(np.uint8), 'P')
    im.putpalette(pal.reshape(-1).tolist())
    im.save(os.path.join(d, name))


def rix(seg, at=0):
    o = seg * 16 + at
    assert img[o:o + 4] == b'RIX3', hex(o)
    w, h = struct.unpack_from('<HH', img, o + 4)
    return w, h, pal6(o + 10), np.frombuffer(img[o + 0x30a:o + 0x30a + w * h], np.uint8).reshape(h, w)


def pbm(seg):
    """IFF PBM at seg:0006 decoded like 14f3 (CMAP >> 2 = 6-bit palette, ByteRun1 BODY, 64000 bytes)"""
    o = seg * 16
    s = img[o:o + 0x10000]
    i = s.find(b'CMAP')
    pal = np.frombuffer(s[i + 8:i + 8 + 768], np.uint8) >> 2
    p = np.clip((pal.astype(np.int32) * 255 + 31) // 63, 0, 255).astype(np.uint8).reshape(256, 3)
    j = s.find(b'BODY') + 8
    out = bytearray()
    while len(out) < 64000:
        n = s[j]; j += 1
        if n < 128:
            out += s[j:j + n + 1]; j += n + 1
        else:
            out += bytes([s[j]]) * (257 - n); j += 1
    return p, np.frombuffer(bytes(out[:64000]), np.uint8).reshape(200, 320), o + j


for seg, name in ((0x273, 'label-fire-clouds'), (0x36e, 'label-fractal-scape')):
    w, h, pal, px = rix(seg)
    save_p('p4-%s.png' % name, px, pal)
w, h, pal, px = rix(0x4eed, 6)  # starts at 4eed:0006, pixels at 4eed:0310 = 4f1e:0000
save_p('p4-nebula.png', px, pal)
print('rix 4eed: %dx%d, pixel range %d..%d' % (w, h, px.min(), px.max()))
pal, px, end = pbm(0x177a)
save_p('p4-chequerboard.png', px, pal)
print('chequerboard: pixel range %d..%d, distinct %d, BODY ends at file offset %#x' % (px.min(), px.max(),
                                                                                  len(np.unique(px)), end))
grey = np.repeat(np.arange(256, dtype=np.uint8)[:, None], 3, 1)
cloud = np.frombuffer(img[0x1ec10:0x1ec10 + 65536], np.uint8).reshape(256, 256)
save_p('p4-cloud.png', np.minimum(cloud * 2, 255), grey)
hm = np.frombuffer(img[0x2eec0:0x2eec0 + 65536], np.uint8).reshape(256, 256)
save_p('p4-landscape-height.png', np.minimum(hm * 2, 255), grey)
cm = np.frombuffer(img[0x3eec0:0x3eec0 + 65536], np.uint8).reshape(256, 256)
lpal = np.zeros((256, 3), np.uint8)
lpal[:16] = pal6(0x100 + 0x20d2, 16)
save_p('p4-landscape-colour.png', cm, lpal)
t = np.frombuffer(img[0x7d90:0x7d90 + 64000], '<u2')[::-1].reshape(200, 160)
save_p('p4-swirlmap.png', np.hstack([t & 255, t >> 8]), grey, SCR)
print('cloud range', cloud.min(), cloud.max(), 'height', hm.min(), hm.max(), 'colour', cm.min(), cm.max())
