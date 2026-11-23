"""Fit the formulas that GFX\\GOURAUD.PAL and GFX\\ANTIALIA.PAL quantise to the TEXTURE.RAW palette.

GOURAUD.PAL row k, entry c: tried as the nearest palette colour to clamp(pal[c] * f) (multiplicative) and to
pal[c] + (63 - pal[c]) * t (toward white); prints the best parameter per level and how many of the texture's
colours it explains. ANTIALIA.PAL entry (a, b): tried as the nearest colour to (pal[a] + pal[b]) / 2 with several
roundings and distance weights.

usage: python3 fit_tables.py
"""
import numpy as np

import engine
from tables import raw_picture


def nearest(target, pal):
    d = ((target[..., None, :] - pal) ** 2).sum(-1)
    return d.argmin(-1), d


def main():
    w, h, pal8, pix = raw_picture('GFX/TEXTURE.RAW')
    pal = (pal8 >> 2).astype(np.float64)
    used = np.nonzero(np.bincount(np.frombuffer(pix, np.uint8), minlength=256))[0]
    G = np.frombuffer(engine.arch_file('GFX/GOURAUD.PAL'), np.uint8).reshape(16, 256)
    print('GOURAUD.PAL, over the %d colours the textures use:' % len(used))
    for k in range(16):
        best = (0, None, None)
        for kind in ('mul', 'white'):
            for f in np.arange(0.0, 6.01, 0.01):
                if kind == 'mul':
                    tgt = np.clip(pal[used] * f, 0, 63)
                else:
                    if f > 1:
                        continue
                    tgt = pal[used] + (63 - pal[used]) * f
                _, d = nearest(tgt, pal)
                ok = (d[np.arange(len(used)), G[k, used]] <= d.min(-1) + 1e-9).mean()
                if ok > best[0]:
                    best = (ok, kind, f)
        print('  level %2d: %-5s %.2f explains %.3f' % (k, best[1], best[2], best[0]))
    T = np.frombuffer(engine.arch_file('GFX/ANTIALIA.PAL'), np.uint8).reshape(256, 256)
    ii = used
    sub = T[np.ix_(ii, ii)]
    print('ANTIALIA.PAL over the used colours:')
    for name, tgt in (('(a+b)/2', (pal[ii][:, None] + pal[ii][None, :]) / 2),
                      ('(a+b)>>1', np.floor((pal[ii][:, None] + pal[ii][None, :]) / 2)),
                      ('8-bit (a+b)/2', (pal8[ii][:, None].astype(float) + pal8[ii][None, :]) / 8)):
        _, d = nearest(tgt, pal)
        ok = (np.take_along_axis(d, sub[..., None].astype(np.int64), -1)[..., 0] <= d.min(-1) + 1e-9).mean()
        print('  nearest to %-14s explains %.3f' % (name, ok))
    # how far the table's colour is from the true average, in 6-bit units
    tgt = (pal[ii][:, None] + pal[ii][None, :]) / 2
    err = np.sqrt(((pal[sub] - tgt) ** 2).sum(-1))
    print('  distance of the table colour from the true average: mean %.2f, 90%% %.2f, max %.2f (6-bit units)' % (
        err.mean(), np.percentile(err, 90), err.max()))
    # entries mapping to colours outside the used range / to index 0..15
    print('  diagonal: T[a][a] == a for %d of %d used colours' % ((np.diag(sub) == ii).sum(), len(ii)))


if __name__ == '__main__':
    main()
