"""Model of p1 (the "Complex Media Labs" logo scroller) written from the decompiled code, checked against
full.frames frame by frame.

Per captured p1 frame: k = number of int 8 handler runs so far (= CRTC start writes / 2, column 'flips' of
full.ticks); the frame shows position pos = 2 * (k - 1) (the handler writes the start address and the panning
before it adds 2), the DAC is the mix of the two 64-colour palettes with the weights of the same pos.

usage: python3 models/core/p1_model.py [--every N]     (from dope/re, inside the toolbox)
"""
import argparse
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
import sheet  # noqa: E402

IMG = open(os.path.join(RE, 'unpacked', 'p1.bin'), 'rb').read()
CS = 0x100                                   # segment 0010
PAL_A = np.frombuffer(IMG[CS + 0x4f0:CS + 0x4f0 + 192], np.uint8).astype(np.int32)   # violet ramp
PAL_B = np.frombuffer(IMG[CS + 0x7f8:CS + 0x7f8 + 192], np.uint8).astype(np.int32)   # red / orange ramp
RIX = 0xe40                                  # 00E4:0000, "RIX3" 1024x200, palette, pixels
assert IMG[RIX:RIX + 4] == b'RIX3'
LOGO = np.frombuffer(IMG[RIX + 10 + 768:RIX + 10 + 768 + 1024 * 200], np.uint8).reshape(200, 1024)


def weight(dx):
    """0b00's weight: dx (signed 16 bit) -> 0..256"""
    dx = dx - 0x10000 if dx & 0x8000 else dx
    if dx < 0:
        dx = 0
    return min(dx >> 4, 0x100)


def table(w):
    """0b00: 64 entries, entry i = high byte of an accumulator that starts at 0 and grows by w while <= 0x3f00"""
    t, ax = [], 0
    for _ in range(64):
        t.append(ax >> 8)
        if ax <= 0x3f00:
            ax += w
    return np.array(t, np.int32)


def dac(pos):
    d = (pos - 0x1f4) & 0xffff
    ta = table(weight((0x1000 - d) & 0xffff))
    tb = table(weight(d))
    return np.minimum(ta[PAL_A] + tb[PAL_B], 63)       # 0b24: 192 bytes -> DAC 0..63


def vram():
    """the 4 planes after 0c8a..0cac: logo rows 1..195 at byte offset 0x50, line = 336 bytes"""
    planes = np.zeros((4, 0x10000), np.uint8)
    for p in range(4):
        for r in range(195):
            row = LOGO[1 + r]
            base = 0x50 + r * 336
            planes[p, base:base + 256] = row[p::4]
    return planes


def screen(planes, pos):
    """320x200 indices as the VGA shows them: start pos >> 4, panning (pos >> 2) & 3, split below line 387"""
    start, pan = pos >> 4, (pos >> 2) & 3
    ys = np.arange(200)
    split = ys * 2 > 0x183
    rowstart = np.where(split, (ys * 2 - 0x184) // 2 * 336, start + ys * 336)
    xs = np.arange(320)[None, :] + pan
    addr = (rowstart[:, None] + (xs >> 2)) & 0xffff
    return planes[xs & 3, addr]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--every', type=int, default=1)
    a = ap.parse_args()
    raw, W, H, rec, hdr, n = sheet.open_frames(os.path.join(RE, 'full.frames'))
    ticks = {}
    for line in open(os.path.join(RE, 'full.ticks')):
        if line.startswith('#'):
            continue
        f = line.split()
        if f[6] == 'p1':
            ticks[int(f[0])] = (int(f[8]), int(f[7], 16))
    planes = vram()
    bad_pix = bad_pal = checked = 0
    for fr in sorted(ticks)[::a.every]:
        flips, start = ticks[fr]
        o = hdr + fr * rec
        pal = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8)
        idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320]
        k = flips // 2
        if k == 0:
            want_idx = np.zeros((200, 320), np.uint8)
            if idx.any():
                bad_pix += 1
            continue
        pos = 2 * (k - 1)
        assert start == pos >> 4, (fr, start, pos)
        checked += 1
        if not np.array_equal(screen(planes, pos), idx):
            bad_pix += 1
            if bad_pix < 5:
                print('pixels differ at frame', fr, 'pos', pos)
        if not np.array_equal(dac(pos), pal[:192]):
            bad_pal += 1
            if bad_pal < 5:
                print('DAC differs at frame', fr, 'pos', pos, np.nonzero(dac(pos) != pal[:192])[0][:10])
    print('checked %d frames: %d with different pixels, %d with a different DAC (0..63)' % (checked, bad_pix, bad_pal))


if __name__ == '__main__':
    main()
