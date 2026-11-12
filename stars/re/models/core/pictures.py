"""Write the overlay's pictures and drawn textures as PNG into stars/re/dump/pictures/fileNN.png (indexed PNG with the
palette the program shows them with; see notes-core.md, "Pictures", for what each one is and how it is used).

- pictures with their own palette (0xfd00 files: 768-byte 6-bit palette + 320x200): that palette;
- sprites / textures whose palette sits at the start of the file (5, 6, 7, 35): those colours at their DAC index;
- backgrounds and maps shown through another file's palette: that palette (47 through palette 42 at index + 0xe6,
  27 and 32 through palette 26 with colour 0 replaced by palette 31's colour 0, 49 through the exe's 0x15e60);
- texture level maps (16: 0..8, 21 / 23 / 43: 0..7, 30: 0..15) as a grey ramp over their value range, because their
  colours come from the 3D shading or a combination table, not from one palette;
- the RGB women of the dissolving part (39, 40, 41 with the run masks 36, 37, 38) as RGBA.
6-bit values v become round(v * 255 / 63). Index 0 is transparent in the sprites 5 and 6, indices >= 0x50 in 32.

usage: python3 pictures.py"""
import os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import cap
import rep
import dissolve_pics

OUT = os.path.join(cap.RE, 'dump', 'pictures')


def pal6(b):
    p = np.frombuffer(bytes(b), np.uint8).reshape(-1, 3).astype(np.int64)
    return ((p * 255 + 31) // 63).astype(np.uint8)


def fpal(n, off=0):
    return pal6(cap.overlay(n)[off:off + 768])


def place(colours, first):
    p = np.zeros((256, 3), np.uint8)
    p[first:first + len(colours)] = colours
    return p


def grey(maxv):
    v = np.arange(256)
    g = np.clip(v * 255 // max(maxv, 1), 0, 255).astype(np.uint8)
    return np.stack([g, g, g], 1)


def save(n, idx, pal, transparent=None):
    im = Image.fromarray(np.ascontiguousarray(idx.astype(np.uint8)), 'P')
    im.putpalette(pal.reshape(-1).tolist())
    kw = {}
    if transparent is not None:
        t = bytes(0 if i in transparent else 255 for i in range(256))
        kw['transparency'] = t
    im.save(os.path.join(OUT, 'file%02d.png' % n), **kw)


def data(n, off=0, size=None, shape=None):
    d = np.frombuffer(cap.overlay(n), np.uint8)[off:]
    if size: d = d[:size]
    if size and len(d) < size: d = np.concatenate([d, np.zeros(size - len(d), np.uint8)])
    return d.reshape(shape) if shape else d


def main():
    os.makedirs(OUT, exist_ok=True)
    # full-screen pictures with their own palette
    for n in (8, 9, 10, 11, 12, 13, 14, 52, 53, 54, 55, 56, 57):
        save(n, data(n, 0x300, 64000, (200, 320)), fpal(n))
    # intro lettering: 4 sprites each, stacked vertically, colour 0 = transparent
    f5 = cap.overlay(5)
    save(5, data(5, 0x2d, 4 * 0x1440, (4 * 36, 144)), place(pal6(f5[:45]), 1), transparent={0})
    f6 = cap.overlay(6)
    save(6, data(6, 0x30, 4 * 0x2500, (4 * 74, 128)), place(pal6(f6[:48]), 0x60), transparent={0})
    # flower petal texture (colour-cycled at run time)
    f7 = cap.overlay(7)
    save(7, data(7, 0xc0, 0x10000, (256, 256)), place(pal6(f7[:0xc0]), 0x10))
    # tunnel colour map
    save(3, data(3, 0, 0x10000, (256, 256)), fpal(4))
    # 3D-part backgrounds
    save(19, data(19, 0, 64000, (200, 320)), fpal(15))
    save(25, data(25, 0, 64000, (200, 320)), fpal(22))
    p26 = fpal(26).copy()
    p26[0] = fpal(31)[0]
    save(27, data(27, 0, 64000, (200, 320)), p26)
    p42 = fpal(42)
    save(47, data(47, 0, 64000, (200, 320)), p42[(np.arange(256) + 0xe6) & 255])
    # "23806 faces" sprite of the bee part, 80x36, indices >= 0x50 transparent
    save(32, data(32, 0, 2880, (36, 80)), p26, transparent=set(range(0x50, 256)))
    # particle text mask
    save(58, data(58, 0, 64000, (200, 320)), grey(31))
    # textures: level maps as grey ramps, direct-index maps with their palette
    save(16, data(16, 0, 0x10000, (256, 256)), grey(8))
    for n in (21, 23, 43):
        save(n, data(n, 0, 0x10000, (256, 256)), grey(7))
    save(30, data(30, 0, 0x10000, (256, 256)), grey(15))
    save(29, data(29, 0, 0x10000, (256, 256)), p26)
    save(35, data(35, 0x300, 0x10000, (256, 256)), fpal(35))
    save(44, data(44, 0, 0x10000, (256, 256)), p42)
    save(49, data(49, 0, 0x10000, (256, 256)), pal6(rep.SEG[0x15e60:0x16160]))
    # the women in RGB (dissolving part)
    for runs, n in ((36, 39), (37, 40), (38, 41)):
        planes, _, _ = dissolve_pics.decode(runs, n)
        rgb = (planes.astype(np.int64) * 255 + 31) // 63
        mask = np.zeros((200, 320), np.uint8)
        # the run list marks the covered pixels: rebuild it from the first plane's runs
        import struct
        r = cap.overlay(runs); o = i = 0
        while True:
            skip = struct.unpack_from('<H', r, i)[0]; i += 2
            if skip == 0xffff: break
            o += skip
            cnt = struct.unpack_from('<H', r, i)[0]; i += 2
            if cnt == 0xffff: break
            mask.reshape(-1)[o:o + cnt] = 255; o += cnt
        rgba = np.dstack([rgb[0], rgb[1], rgb[2], mask]).astype(np.uint8)
        Image.fromarray(rgba, 'RGBA').save(os.path.join(OUT, 'file%02d.png' % n))
    print('wrote', sorted(os.listdir(OUT)))


if __name__ == '__main__':
    main()
