"""Write the hand-drawn pictures p3 and p5 use as palette PNGs into dope/re/dump/pictures/ (git-ignored):
ColoRIX (RIX3) files embedded in the images: "RIX3", u16 width, u16 height, 0xAF (256-colour VGA palette),
0x00 (uncompressed), 768-byte 6-bit palette, width*height pixels. Plus p5's chequerboard as p5 shows it (p4's last
frame in VGA memory & 0x0f, with the DAC p4 leaves)."""
import os, struct, pickle
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
OUT = os.path.join(RE, 'dump', 'pictures'); os.makedirs(OUT, exist_ok=True)

def rix(part, off):
    img = open(os.path.join(RE, 'unpacked', part + '.bin'), 'rb').read()
    assert img[off:off + 4] == b'RIX3'
    w, h, pt, st = struct.unpack_from('<HHBB', img, off + 4)
    pal = np.frombuffer(img[off + 10:off + 778], np.uint8).reshape(256, 3)
    pix = np.frombuffer(img[off + 778:off + 778 + w * h], np.uint8).reshape(h, w)
    return pix, pal

def save(name, pix, pal):
    im = Image.fromarray(pix, 'P')
    im.putpalette(((pal.astype(np.uint16) * 255 + 31) // 63).astype(np.uint8).ravel().tolist())
    im.save(os.path.join(OUT, name))
    print(name, pix.shape[1], 'x', pix.shape[0], 'colours used', np.unique(pix).min(), '..', np.unique(pix).max())

save('p3-nebula.png', *rix('p3', 0x4b806))
save('p3-label-phong.png', *rix('p3', 0x33420))
save('p5-envmaps.png', *rix('p5', 0x3a66))
save('p5-label-env.png', *rix('p5', 0x248f0))
save('p5-label-bump.png', *rix('p5', 0x258a0))
save('p5-swirl.png', *rix('p5', 0x4ea06))
d = pickle.load(open(os.path.join(RE, 'snaps', 'part_p5.snap'), 'rb'))
v = (np.frombuffer(d['vram'], np.uint8)[:64000] & 15).reshape(200, 320).astype(np.uint8)
pal = np.zeros((256, 3), np.uint8)
pal[:16] = np.frombuffer(bytes(d['vga']['dac']), np.uint8).reshape(256, 3)[:16]
save('p5-chequerboard.png', v, pal)
