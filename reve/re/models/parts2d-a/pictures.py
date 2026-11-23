"""Write the pictures that parts2d-a's entries draw as they are, with the palette the demo shows them with and their
transparent index as alpha, to reve/re/dump/pictures/<NAME>.png (ignored by Git; named after the RAW file, as the other scopes do), for the
redraws.

usage: python3 pictures.py [--tiles DIR]

entry 0   GFX\\REVE.RAW     through GFX\\TEXTURE.RAW's palette (REVE's own is the same), index 0 transparent
entry 5/6 GFX\\PULSE.RAW    its own palette, opaque (entry 5 also shows it through GFX\\NEGATIF.PAL: computed)
entry 7   GFX\\WIZ.RAW      its own palette (= TEXTURE's), index 0 transparent
entry 9/10 GFX\\PLAYMO.RAW  its own palette, opaque (the rings move its pixels, they do not recolour them)
entry 15  GFX\\BOUCHE.RAW   its own palette, opaque (as PLAYMO)
entry 12/13 GFX\\SPIRALE.RAW's top-left 6 x 6 (the dot), through TEXTURE's palette, index 0 transparent: SPIRALE-dot
the rotozoom textures, through TEXTURE's palette (tiling textures, candidates for a tileable upscale, not a redraw):
          TEXTURE-tile5 (64 x 64 at 0x4040: entry 7 layer 2), TEXTURE-6010 (16 x 16: entry 7 layer 1),
          TEXTURE-5010 (16 x 16: entries 12/13)
--tiles DIR also writes DIR/texture-blocks.png: GFX\\TEXTURE.RAW (2x) with the blocks the rotozoomers copy outlined.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

from common import RE, raw, pal6_to_rgb

OUT = os.path.join(RE, 'dump', 'pictures')


def rgba(px, pal8, transparent=None):
    lut = pal6_to_rgb(pal8 >> 2)          # what the DAC shows: 6-bit v = 8-bit >> 2, shown as v / 63
    im = lut[px]
    a = np.full(px.shape, 255, np.uint8)
    if transparent is not None:
        a[px == transparent] = 0
    return np.dstack([im, a])


def main():
    os.makedirs(OUT, exist_ok=True)
    tex_px, tex_pal = raw('GFX\\TEXTURE.RAW')
    jobs = [('REVE', 'GFX\\REVE.RAW', tex_pal, 0),
            ('PULSE', 'GFX\\PULSE.RAW', None, None),
            ('WIZ', 'GFX\\WIZ.RAW', None, 0),
            ('PLAYMO', 'GFX\\PLAYMO.RAW', None, None),
            ('BOUCHE', 'GFX\\BOUCHE.RAW', None, None)]
    for name, f, pal, tr in jobs:
        px, own = raw(f)
        Image.fromarray(rgba(px, own if pal is None else pal, tr), 'RGBA').save(os.path.join(OUT, name + '.png'))
        print('wrote %s.png  %dx%d' % (name, px.shape[1], px.shape[0]))
    spx, _ = raw('GFX\\SPIRALE.RAW')
    blocks = [('SPIRALE-dot', spx[:6, :6], 0), ('TEXTURE-tile5', tex_px[64:128, 64:128], None),
              ('TEXTURE-6010', tex_px[96:112, 16:32], None), ('TEXTURE-5010', tex_px[80:96, 16:32], None)]
    for name, px, tr in blocks:
        Image.fromarray(rgba(px, tex_pal, tr), 'RGBA').save(os.path.join(OUT, name + '.png'))
        print('wrote %s.png  %dx%d' % (name, px.shape[1], px.shape[0]))
    if len(sys.argv) < 3 or sys.argv[1] != '--tiles':
        return
    im = Image.fromarray(rgba(tex_px, tex_pal)[:, :, :3]).resize((512, 512), Image.NEAREST)
    d = ImageDraw.Draw(im)
    for (x, y, s, c) in ((32, 64, 16, 'yellow'), (16, 96, 16, 'cyan'), (64, 64, 64, 'magenta'), (16, 80, 16, 'lime')):
        d.rectangle((2 * x, 2 * y, 2 * (x + s) - 1, 2 * (y + s) - 1), outline=c)
    im.save(os.path.join(sys.argv[2], 'texture-blocks.png'))
    print('wrote texture-blocks.png')


if __name__ == '__main__':
    main()
