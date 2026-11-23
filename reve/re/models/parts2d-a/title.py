"""Entry 0 (init 0x204840, frame 0x20489c): GFX\\REVE.RAW drawn by the sprite routine 0x25f0be, centred, on black,
through GFX\\TEXTURE.RAW's palette.

usage: python3 title.py
Compares every record from the entry's first frame (record 32: the init takes two refreshes, so the first frame is
drawn at once) to the last before entry 1's first frame with the model.
"""
import numpy as np

from common import frames, indexed, raw


def sprite(dst, src, w, h, stride):
    """0x25f0be: ESI src, BL width, BH height, EDX source stride, EDI buffer: top-left at
    (160 - (w >> 1), 100 - (h >> 1)); index 0 is not drawn; no clipping."""
    y0, x0 = 100 - (h >> 1), 160 - (w >> 1)
    s = src.reshape(-1)
    for r in range(h):
        row = s[r * stride:r * stride + w]
        d = dst[y0 + r, x0:x0 + w]
        d[row != 0] = row[row != 0]


def main():
    raw_frames, n = frames()
    reve, _ = raw('GFX\\REVE.RAW')
    _, tpal = raw('GFX\\TEXTURE.RAW')
    buf = np.zeros((200, 320), np.uint8)
    sprite(buf, reve, 0xe3, 0x50, 0xe3)
    bad = []
    first, last = 32, 651
    for r in range(first, last + 1):
        px, dac = indexed(raw_frames, r)
        if not ((px == buf).all() and (dac == tpal >> 2).all()):
            bad.append(r)
    print('records %d..%d: %d differ %s' % (first, last, len(bad), bad[:10]))
    print('picture at x %d..%d, y %d..%d' % (160 - 113, 160 - 113 + 226, 100 - 40, 100 + 39))


if __name__ == '__main__':
    main()
