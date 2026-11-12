"""Model of the intro's name cards (0x5c68 / 0x5e7a / 0x5ece, tables 0x5c97 / 0x62bb, palette 0x61b1 / 0x615b).

Builds the polar map of each card exactly like 0x5c97, draws the texture of file 7 through it with the offsets the
hook logged at 0x5ed8 (run_hook.py --hook 0x5ed8:5410/2,5412/2,5a77/24,5465/4,541e/4), computes the palette like
0x61b1 + 0x615b from the card start, and compares every frame with the capture.

usage: python3 flower_model.py runs/intro [--save]
"""
import sys

import numpy as np

from common import *

SIN = sintab()
f5, f6, f7 = ofile(5), ofile(6), ofile(7)
TEX = np.frombuffer(f7[0xc0:0xc0 + 0x10000], np.uint8)

CARDS = {}
for a in (0x542a, 0x5437, 0x5444, 0x5451):
    CARDS[a] = dict(img6=rd16(a), img5=rd16(a + 2), shift=IMG[a + 4], step=rd32(a + 5))


def card_text(c):
    """0x5e63: the word of file 5 (144x36 at x 88, y 150) and the name of file 6 (128x74 at x 96, y 60), 0 = clear."""
    scr = np.zeros(64000, np.uint8)
    w5 = np.frombuffer(f5[0x2d + c['img5'] * 0x1440:][:0x1440], np.uint8).reshape(36, 144)
    w6 = np.frombuffer(f6[0x30 + c['img6'] * 0x2500:][:0x2500], np.uint8).reshape(74, 128)
    s = scr.reshape(200, 320)
    for img, x0, y0 in ((w5, 88, 150), (w6, 96, 60)):
        h, w = img.shape
        reg = s[y0:y0 + h, x0:x0 + w]
        reg[img != 0] = img[img != 0]
    return scr


def build_map(c):
    """0x5c97: 4096 rays from (160, 100); texel word (v << 8 | u), 0xffff = not drawn."""
    scr = card_text(c)
    m = np.full(64000, 0xffff, np.int64)
    cl, ebp = c['shift'], c['step']
    r = [((int(SIN[(((i * ebp) & 0xffffffff) & 0x3ffc) >> 2]) + 0x10000) >> cl) for i in range(4096)]  # 0x1400 table
    for i in range(4096):
        d = r[i] + 1
        n = 100 - d
        cs, sn = int(SIN[i + 1024]), int(SIN[i])
        x, y = 0xa00000, 0x640000
        bp = (((i * 4) << 3) & 0xff00) | (d & 0xff)
        while n > 0:
            y = (y + sn) & 0xffffffff
            if y >= 0xc80000: break
            x = (x + cs) & 0xffffffff
            if x >= 0x1400000: break
            k = (y >> 16) * 320 + (x >> 16)
            if scr[k] == 0:
                m[k] = bp
            bp = (bp + 1) & 0xffff
            n -= 1
    return m, scr


def base_palette():
    P = np.zeros(768, np.int64)
    P[3:48] = np.frombuffer(f5[:45], np.uint8)
    P[0x60 * 3:0x70 * 3] = np.frombuffer(f6[:48], np.uint8)
    P[0x10 * 3:0x50 * 3] = np.frombuffer(f7[:0xc0], np.uint8)
    return P


def band_scale(ph):
    return ((int(SIN[(ph & 0x3ffc) >> 2]) + 0x30000) >> 10) & 0xff


def work_palette(P, ph):
    """0x61b1 after the phase steps: ph = [0x5a7a, 0x5a7e, 0x5a82, 0x5a86, 0x5a8a]."""
    W = P[:0x150].copy()
    for j in range(4):
        s = band_scale(ph[j + 1])
        W[0x30 * (j + 1):0x30 * (j + 2)] = (P[0x30 * (j + 1):0x30 * (j + 2)] * s) >> 8
    s = band_scale(ph[0])
    W[0x120:0x150] = (P[0x120:0x150] * s) >> 8
    return W


def main():
    pre = sys.argv[1]
    frames = Frames(pre + '.frames')
    f0 = 279
    rows = [l.split() for l in open(pre + '.hook') if l.startswith('5ed8 ')]
    P = base_palette()
    maps = {}
    stats = {}
    for r in rows:
        f = int(r[1])
        mem = r[11:]
        v5410 = int.from_bytes(bytes.fromhex(mem[0]), 'little')
        v5412 = int.from_bytes(bytes.fromhex(mem[1]), 'little')
        b = bytes.fromhex(mem[2])
        lvl_after = b[0]
        ph = [int.from_bytes(b[3 + 4 * k:7 + 4 * k], 'little') for k in range(5)]
        card = int.from_bytes(bytes.fromhex(mem[4]), 'little')
        if card not in maps:
            maps[card] = build_map(CARDS[card])
        m, scr = maps[card]
        si = ((v5410 & 0xff) << 8) | (v5412 & 0xff)
        vga = scr.copy()
        sel = m != 0xffff
        vga[sel] = TEX[(m[sel] + si) & 0xffff]
        cap = frames.vga(f - f0).reshape(-1)
        diff = int(np.count_nonzero(cap != vga))
        # palette: bl used = level before the step (the hook runs after it)
        W = work_palette(P, ph)
        # level before the step: reconstruct from the logged count/step (byte 1 = step, byte 2 = count left)
        step = b[1] - 256 if b[1] >= 128 else b[1]
        cnt = b[2]
        # count was decremented if it was > 0 before; the level read was lvl_after - step in that case
        stats.setdefault(card, []).append((f, diff, None))
        pal = frames.pal(f - f0).reshape(-1).astype(np.int64)
        bl_cands = [lvl_after, (lvl_after - step) & 0xff]
        okpal = None
        for bl in bl_cands:
            D = np.zeros(768, np.int64)
            D[:0x150] = ((W * bl) >> 7) & 0xff
            if np.array_equal(D[:0x210], pal[:0x210]):
                okpal = bl
                break
        stats[card][-1] = (f, diff, okpal)
        if diff or okpal is None:
            print('frame', f, 'card %x' % card, 'pixels differing', diff, 'palette', 'ok' if okpal is not None else 'DIFF',
                  'lvl', lvl_after, 'ph', [hex(x) for x in ph])
        if '--save' in sys.argv and f in (1250, 1400, 2200, 3100, 4000):
            save('flower_%d_model.png' % f, rgb(vga.reshape(200, 320), frames.pal(f - f0)), 2)
            save('flower_%d_capture.png' % f, rgb(cap.reshape(200, 320), frames.pal(f - f0)), 2)
    for c, l in stats.items():
        print('card %x: %d frames %d..%d, %d with pixel differences, %d with palette differences' % (
            c, len(l), l[0][0], l[-1][0], sum(1 for x in l if x[1]), sum(1 for x in l if x[2] is None)))


if __name__ == '__main__':
    main()
