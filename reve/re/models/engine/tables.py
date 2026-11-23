"""How GFX\\ANTIALIA.PAL and GFX\\GOURAUD.PAL relate to the palette of GFX\\TEXTURE.RAW, which palette each 3D part
shows (from the capture's DAC), and whether the texture buffer [0x21b140] holds TEXTURE.RAW's pixels.

usage: python3 tables.py
"""
import os
import sys

import numpy as np

import engine

sys.path.insert(0, engine.RE)
from sheet import open_frames, indexed  # noqa: E402


def raw_picture(name):
    d = engine.arch_file(name)
    w, h = int.from_bytes(d[8:10], 'big'), int.from_bytes(d[10:12], 'big')
    pal = np.frombuffer(d[32:32 + 768], np.uint8).reshape(256, 3)
    return w, h, pal, d[800:800 + w * h]


def main():
    w, h, pal8, pix = raw_picture('GFX/TEXTURE.RAW')
    pal = (pal8 >> 2).astype(np.int64)          # 6-bit DAC values as the loader writes them
    print('TEXTURE.RAW %dx%d' % (w, h))
    # ANTIALIA.PAL: T[a*256+b] vs the nearest palette colour to the average of a and b
    T = np.frombuffer(engine.arch_file('GFX/ANTIALIA.PAL'), np.uint8).reshape(256, 256)
    print('ANTIALIA symmetric:', np.array_equal(T, T.T), ' diagonal = identity:',
          np.array_equal(np.diag(T), np.arange(256)))
    avg = (pal[:, None, :] + pal[None, :, :]) / 2.0
    d2 = ((avg[:, :, None, :] - pal[None, None, :, :]) ** 2).sum(-1)       # 256 x 256 x 256
    best = d2.min(-1)
    got = d2[np.arange(256)[:, None], np.arange(256)[None, :], T]
    print('ANTIALIA entries that are a nearest colour to the 6-bit average: %.4f' % (got == best).mean(),
          ' mean extra distance %.3f' % (got - best).mean(), ' max %.1f' % (got - best).max())
    # 0x25a275 builds such a table from the palette: compare it with the file below (see notes)
    # GOURAUD.PAL: level k of colour c
    G = np.frombuffer(engine.arch_file('GFX/GOURAUD.PAL'), np.uint8).reshape(16, 256)
    print('GOURAUD level 0 = identity: %.3f' % (G[0] == np.arange(256)).mean())
    for k in (0, 4, 8, 12, 15):
        ratio = []
        for c in range(256):
            s = pal[c].sum()
            if s > 30:
                ratio.append(pal[G[k, c]].sum() / s)
        print('  level %2d: mean brightness ratio %.3f (min %.3f, max %.3f)' % (k, np.mean(ratio), np.min(ratio),
                                                                              np.max(ratio)))
    # brightness model: level k -> colour * (1 - k / 16)? nearest-colour check
    for k in (4, 8, 15):
        for f in ((16 - k) / 16, (15 - k) / 15, 1 - k / 32, (31 - k) / 31):
            tgt = pal * f
            dd = ((tgt[:, None, :] - pal[None, :, :]) ** 2).sum(-1)
            hit = (dd[np.arange(256), G[k]] == dd.min(-1)).mean()
            print('  level %2d vs nearest to colour*%.3f: %.3f' % (k, f, hit))
    raw, nrec = open_frames(os.path.join(engine.RE, 'full.frames'))
    for name, a, b in (('INTRO', 652, 2736), ('P3D1', 4097, 6328), ('PART2', 7230, 8119), ('P3D3', 9915, 10806),
                       ('PART4', 14492, 15377), ('FIN', 16276, 16720)):
        pals = {}
        for k in range(a, b, 10):
            p = indexed(raw, k)[1].tobytes()
            pals.setdefault(p, []).append(k)
        first = list(pals)[0]
        same = np.array_equal(np.frombuffer(first, np.uint8).reshape(256, 3), pal)
        print('%s: %d distinct palettes in the capture; first = TEXTURE.RAW palette: %s; first seen %s' % (
            name, len(pals), same, [v[0] for v in pals.values()][:6]))


if __name__ == '__main__':
    main()
