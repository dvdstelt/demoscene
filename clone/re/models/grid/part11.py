"""Model of part 11 (0x419dc7): 8 x 6 tiles of 40 x 24 pixels from the colour table 0x428164 (entries 8..55),
fading in by the tile clock [0xf73934], fading out by [0x428464] after event 1101; the black box and the title.

usage: python3 models/grid/part11.py   (from clone/re; needs dump/grid/p11.pkl from run_clone.py --dump)
Checks the state update and every tile pixel of the dumped frames (the title is blended by 0x404002 / 0x40442c,
core scope; only its position is checked).
"""
import math
import os
import pickle
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
IMG = open(os.path.join(RE, 'unpacked', 'clone_img.bin'), 'rb').read()


def f32t(x):
    f = np.float32(x)
    if abs(float(f)) > abs(x): f = np.nextafter(f, np.float32(0))
    return float(f)


def F(d, a): return struct.unpack('<f', d[a][:4])[0]


def table(i):
    """int i of the colour table at 0x428164 (dwords, read from the exe)."""
    return struct.unpack_from('<i', IMG, 0x428164 + 4 * i - 0x400000)[0]


def tile_colour(row, col, clock, fade):
    k = 24 + 3 * (row * 8 + col)                     # edx starts at 0x18: entries 8..55
    R, G, B = table(k), table(k + 1), table(k + 2)
    lvl = f32t((math.sin(R * clock * 0.01 + R) * 0.3 + 0.7) * (clock * 0.13))
    if lvl > 1.0: lvl = 1.0
    out = []
    for c in (R, G, B):
        v = int(math.trunc(c * 0.0125 * 255.0 * lvl * fade))
        out.append(min(v, 255))
    if 2 < col < 7 and 1 < row < 4:
        return 0
    return out[0] << 16 | out[1] << 8 | out[2]


def main():
    recs = [r for r in pickle.load(open(os.path.join(RE, 'dump', 'grid', 'p11.pkl'), 'rb')) if r['part'] == 11]
    bad = 0
    state_err = 0.0
    for r in recs:
        e, x = r['entry'], r['exit']
        dt = F(e, 0x420489)
        clock = f32t(dt * 0.05 + F(e, 0xf73934))
        fade = F(e, 0x428464)
        if struct.unpack('<i', x[0x428468])[0] & 0x7fffffff and fade > 0:
            fade = f32t(fade - dt * 0.0065)
            if fade < 0: fade = 0.0
        state_err = max(state_err, abs(clock - F(x, 0xf73934)), abs(fade - F(x, 0x428464)))
        page = np.frombuffer(r['final'], '<u4').reshape(200, 320)
        for row in range(6):
            for col in range(8):
                blk = page[24 + 24 * row:48 + 24 * row, 40 * col:40 * col + 40]
                want = tile_colour(row, col, clock, fade)
                if 2 < col < 7 and 1 < row < 4:          # the black box: black except the title's pixels
                    title = np.zeros((200, 320), bool)
                    title[104:113, 141:242] = True
                    m = ~title[24 + 24 * row:48 + 24 * row, 40 * col:40 * col + 40]
                    bad += int((blk[m] != 0).sum())
                else:
                    bad += int((blk != want).sum())
    print('part 11: %d frames, state max error %g, tile pixels that differ: %d' % (len(recs), state_err, bad))


if __name__ == '__main__':
    main()
