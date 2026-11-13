"""Check the picture overlays of parts 2, 5, 3, 4, 1 and 11 against the capture full.frames.

usage (inside the toolbox, from clone/re): python3 models/gfx/overlay_check.py [tick ...]

For each tick: the part (full.ticks), then for every overlay of that part (OVERLAYS below: the black box drawn
by 0x4043be and the pictures drawn by 0x404002 at fixed positions) the number of pixels that match
  box:      captured == 0 where no picture covers it,
  picture:  opaque blit (level <= 0): captured == picture pixel; blend (level 1..63):
            captured == src + trunc((dst - src) * level / 64) per channel (0x40442c with the table 0x403f10),
            solved for the level that fits best when the level is a run-time variable.
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
sys.path.insert(0, HERE)
import pictures  # noqa: E402

PALS = pictures.palettes()
PIC = {}
for name, addr, w, h, pn, mode, ptr in pictures.PICTURES:
    _, px = pictures.decode(addr, w, h, PALS[pn], mode)
    a = np.array(px, dtype=np.uint64).reshape(h, w)
    PIC[name] = a

# part -> (box (x, y, w, h) or None, [(picture, x, y, level or None = variable)])
OVERLAYS = {
    2: ((10, 130, 100, 40), [('meet_us', 20, 151, 0), ('glyph1', 80, 111, 0)]),
    5: ((180, 30, 130, 40), [('get_them_clones', 215, 53, 0), ('glyph2', 140, 0, 0)]),
    3: ((162, 135, 148, 40), [('connect_the_dots', 212, 160, None), ('glyph3', 212, 87, None)]),
    4: ((10, 45, 170, 40), [('tunnels_meet_more_tunnels', 20, 70, 0), ('glyph4', 40, 0, 0)]),
    1: (None, [('title_clone_meets_clone', 141, 104, None), ('credits_aap_echo_simon', 141, 74, None)]),
    11: (None, [('title_clone_meets_clone', 141, 104, None)]),
}


def load():
    raw = np.memmap(os.path.join(RE, 'full.frames'), dtype=np.uint8, mode='r')
    w, h, bits, rec, hdr, mode = struct.unpack('<IIIIII', bytes(raw[8:32]))
    ticks = np.loadtxt(os.path.join(RE, 'full.ticks'), comments='#', usecols=range(9))
    return raw, rec, hdr, ticks


def frame(raw, rec, hdr, t):
    return np.asarray(raw[hdr + t * rec:hdr + (t + 1) * rec]).reshape(200, 320, 3).astype(int)


def blend(src, dst, level):
    """0x40442c per channel: src + trunc((dst - src) * level / 64)."""
    return src + np.trunc((dst - src) * level / 64).astype(int)


def check(raw, rec, hdr, ticks, t, prev=None):
    row = ticks[ticks[:, 0] == t][0]
    part = int(row[6])
    f = frame(raw, rec, hdr, t)
    print(f'tick {t}: part {part}')
    if part not in OVERLAYS: return
    box, pics = OVERLAYS[part]
    covered = np.zeros((200, 320), bool)
    for name, x, y, level in pics:
        p = PIC[name]
        h, w = p.shape
        opaque = p != 0xffffffff
        src = np.stack([(p >> 16) & 255, (p >> 8) & 255, p & 255], -1).astype(int)
        cap = f[y:y + h, x:x + w]
        if level is not None and level <= 0:
            ok = np.all(cap == src, -1) & opaque
            print(f'  {name} at ({x},{y}) opaque: {ok.sum()} of {opaque.sum()} pixels match')
        else:
            # the background under the picture is unknown; where the box is black, dst = 0
            if box is not None:
                bx, by, bw, bh = box
                yy, xx = np.mgrid[y:y + h, x:x + w]
                onbox = (xx >= bx) & (xx < bx + bw) & (yy >= by) & (yy < by + bh)
            else:
                onbox = np.ones((h, w), bool)    # parts 1, 11: assume black behind (check the score)
            best = None
            for lv in range(0, 64):
                pred = src if lv == 0 else blend(src, 0, lv)
                ok = np.all(cap == pred, -1) & opaque & onbox
                if best is None or ok.sum() > best[1]: best = (lv, ok.sum())
            print(f'  {name} at ({x},{y}) blend: best level {best[0]}: {best[1]} of {(opaque & onbox).sum()} '
                  f'pixels over black match')
        covered[y:y + h, x:x + w] |= opaque
    if box is not None:
        bx, by, bw, bh = box
        reg = f[by:by + bh, bx:bx + bw]
        free = ~covered[by:by + bh, bx:bx + bw]
        print(f'  box ({bx},{by}) {bw}x{bh}: {np.all(reg == 0, -1)[free].sum()} of {free.sum()} uncovered pixels black')


def main():
    raw, rec, hdr, ticks = load()
    ts = [int(a) for a in sys.argv[1:]] or [2600, 3000, 6500, 6900, 8000, 8500, 9300, 9800, 1560, 1600, 1640,
                                            1700, 2124, 2160, 2200, 11590, 11620, 11700]
    for t in ts: check(raw, rec, hdr, ticks, t)


if __name__ == '__main__':
    main()
