"""What GFX\\SPIRBLUR.PAL (the 256 x 256 table of entries 12 and 13) is, and what 0x25a275 computes.

usage: python3 blendtables.py

0x25a275 (no call site in the image) fills 0x20643c with T[a * 256 + b] = the palette index nearest (squared RGB
distance, first of equals) to (3 * pal[b] + pal[a]) >> 2 per channel, pal = 0x20613c (6 bits). This compares
SPIRBLUR.PAL with that table and with other weights, for TEXTURE.RAW's palette (the DAC in entries 12 and 13).
"""
import numpy as np

from common import raw, file


def nearest_table(pal6, wa, wb, rounding):
    p = pal6.astype(np.int64)
    a = p[:, None, :]
    b = p[None, :, :]
    tgt = (wa * a + wb * b + rounding) // (wa + wb)                # 256 x 256 x 3
    d = ((tgt[:, :, None, :] - p[None, None, :, :]) ** 2).sum(-1)  # 256 x 256 x 256
    return d.argmin(-1).astype(np.uint8)


def main():
    _, pal = raw('GFX\\TEXTURE.RAW')
    pal6 = pal >> 2
    t = np.frombuffer(file('GFX\\SPIRBLUR.PAL'), np.uint8).reshape(256, 256)
    tex, _ = raw('GFX\\TEXTURE.RAW')
    used = np.unique(tex[80:96, 16:32])
    print('TEXTURE block 0x5010 uses %d indices' % len(used))
    for wa, wb, rnd in ((1, 3, 0), (1, 1, 0), (1, 1, 1), (3, 1, 0), (1, 3, 2), (1, 2, 0), (2, 1, 0), (1, 7, 0),
                        (7, 1, 0)):
        m = nearest_table(pal6, wa, wb, rnd)
        eq = (m == t)
        # also compare colours: how far the table's colour is from the weighted mean
        print('T[a][b] = nearest((%d a + %d b + %d) / %d): %d of 65536 equal' % (wa, wb, rnd, wa + wb, eq.sum()))
    p = pal6.astype(float)
    ca = p[:, None, :]
    cb = p[None, :, :]
    ct = p[t]
    # least squares: ct = w * ca + (1 - w) * cb
    x = (ca - cb).reshape(-1)
    y = (ct - cb).reshape(-1)
    w = (x * y).sum() / (x * x).sum()
    print('least-squares weight of a (the zoomed source): %.4f' % w)
    print('diagonal T[i][i] == i: %d of 256' % (t[np.arange(256), np.arange(256)] == np.arange(256)).sum())


if __name__ == '__main__':
    main()
