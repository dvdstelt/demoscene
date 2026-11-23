"""The still pictures of script entries 16, 17, 24, 25, 29 and the end (0x20538d), and the picture dumps.

usage (from reve/re, in the toolbox):
  python3 models/parts2d-b/pictures.py dump            write dump/pictures/<NAME>.png for every picture of this scope
  python3 models/parts2d-b/pictures.py check           model each still screen and compare it with full.frames

The inits clear 0xa0000 to index 0, load the picture with its palette (0x25a1fb, EAX = 1: DAC = v >> 2) and either
load it straight to 0xa0000 (320 x 200: MRWAAF, YOCOL) or into [0x205e80] and draw it with 0x25f0be, which centres a
w x h sprite (BL = w, BH = h, EDX = source stride) at x0 = 160 - (w >> 1), y0 = 100 - (h >> 1), index 0 not drawn.
"""
import os
import sys

import numpy as np
from PIL import Image

from common import DUMP, raw, pal6, to_rgb, frames, indexed, entry_starts

# entry -> (picture, how): 'sprite' = 0x25f0be centred, index 0 transparent; 'full' = loaded to 0xa0000
STILLS = {16: ('JUNIOR', 'sprite'), 17: ('MRTOMMY', 'sprite'), 24: ('DOG', 'sprite'), 25: ('MRWAAF', 'full'),
          29: ('YOCOL', 'full'), 'end': ('REVE', 'sprite')}

# every picture of this scope that is shown as it is, with the palette it is shown with and its transparent index
DUMPS = {
    'JUNIOR': ('JUNIOR', 0), 'MRTOMMY': ('MRTOMMY', 0), 'DOG': ('DOG', 0), 'REVE': ('REVE', 0),
    'MRWAAF': ('MRWAAF', None), 'YOCOL': ('YOCOL', None), 'GENE': ('GENE', None), 'BOUGE': ('BOUGE', None),
    # textures of the interlaced planes (entries 18/19 and 23), mapped as they are (no table), tiled
    'DADA': ('DADA', None), 'TOM': ('TOM', None), 'BOZ': ('TOM', None),
}


def screen(name, how):
    """The 320 x 200 indices and the 6-bit palette the init leaves on screen."""
    px, pal = raw(name)
    scr = np.zeros((200, 320), np.uint8)
    if how == 'full':
        scr[:] = px
    else:
        h, w = px.shape
        y0, x0 = 100 - (h >> 1), 160 - (w >> 1)
        reg = scr[y0:y0 + h, x0:x0 + w]
        reg[px != 0] = px[px != 0]
    return scr, pal6(pal)


def dump():
    os.makedirs(DUMP, exist_ok=True)
    for out, (palname, transparent) in DUMPS.items():
        px, _ = raw(out)
        _, pal = raw(palname)
        rgb = to_rgb(px, pal6(pal))
        a = np.full(px.shape, 255, np.uint8)
        if transparent is not None:
            a[px == transparent] = 0
        Image.fromarray(np.dstack([rgb, a]), 'RGBA').save(os.path.join(DUMP, out + '.png'))
        print('%-8s %3d x %3d  palette %-7s transparent %s' % (out, px.shape[1], px.shape[0], palname, transparent))
    # the 64 x 64 tile of GFX\TEXTURE.RAW that entries 18/19 map on their even lines (DADA's palette = TEXTURE's)
    px, pal = raw('TEXTURE')
    Image.fromarray(to_rgb(px[:64, :64], pal6(pal))).save(os.path.join(DUMP, 'TEXTURE-tile0.png'))
    print('TEXTURE-tile0  64 x 64  palette TEXTURE (= DADA)')


def check():
    raw_, n = frames()
    init, setf = entry_starts()
    for e, (name, how) in STILLS.items():
        if e == 'end':
            k0 = max(setf.values()) if setf else None
            if 29 not in setf:
                print('end: capture not there yet'); continue
            # the end screen follows entry 29; find the first record that shows REVE
            want, wpal = screen(name, how)
            ks = [k for k in range(setf[29], n) if (indexed(raw_, k)[0] == want).all()]
            if not ks:
                print('end: REVE not found'); continue
            print('end: REVE from record %d to %d (%d records)' % (ks[0], ks[-1], len(ks)))
            continue
        if e not in setf:
            print('entry %d (%s): capture not there yet' % (e, name)); continue
        want, wpal = screen(name, how)
        last = setf.get(e + 1, init.get(e + 1, n)) - 1
        same = [k for k in range(init[e], min(last + 1, n))
                if (indexed(raw_, k)[0] == want).all() and (indexed(raw_, k)[1] == wpal).all()]
        print('entry %d (%s): init %d, retrace set %d, next entry %d: %d records identical to the model (%s..%s)'
              % (e, name, init[e], setf[e], last + 1, len(same), same[:1], same[-1:]))


if __name__ == '__main__':
    {'dump': dump, 'check': check}[sys.argv[1]]()
