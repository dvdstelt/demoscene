"""Check the picture fades (0x17e50 show, 0x17e96 fade, 0x17eba fade out + free; 0x4a7b0 / 0x4a80b / 0x4a720)
against full.frames: for every captured frame of main's picture sequences, find the factor f with
DAC == min(63, (pal * f) >> 8) for the shown picture's palette, and print the runs (frame range -> factors).

usage: python3 fades.py [first last file]   (default: the picture-57 and credits sequences)"""
import sys
import numpy as np
import cap


def scale(pal, f):
    return np.minimum(63, (pal.astype(np.int64) * f) >> 8)


def factor_of(dac, pal):
    hits = [f for f in range(0, 257) if np.array_equal(scale(pal, f), dac)]
    return hits


def run(first, last, files):
    pals = {n: np.frombuffer(cap.overlay(n)[:768], np.uint8).reshape(256, 3) for n in files}
    t = cap.ticks()
    prev = None
    start = first
    out = []
    for fr in range(first, last + 1):
        dac = cap.palette(fr)
        key = None
        for n, pal in pals.items():
            h = factor_of(dac, pal)
            if h:
                key = (n, h[0], h[-1])
                break
        if key is None: key = ('?',)
        if key != prev:
            out.append((fr, key, t[fr][4], t[fr][5], t[fr][8]))
            prev = key
    return out


if __name__ == '__main__':
    if len(sys.argv) > 3:
        seqs = [(int(sys.argv[1]), int(sys.argv[2]), [int(x) for x in sys.argv[3:]])]
    else:
        seqs = [(15972, 16642, [57]), (25163, 26667, [54, 53, 55, 52, 10, 56])]
    for first, last, files in seqs:
        rows = run(first, last, files)
        print(f'--- frames {first}..{last}, files {files}')
        # merge single-frame steps into ramps: consecutive frames whose factor range contains f0 + k * step
        i = 0
        while i < len(rows):
            fr, key, o, r, drawn = rows[i]
            nxt = rows[i + 1][0] - 1 if i + 1 < len(rows) else last
            j = i
            if nxt == fr and len(key) == 3 and i + 1 < len(rows) and len(rows[i + 1][1]) == 3:
                k1 = rows[i + 1][1]
                for step in (4, -4, 8, -8, 2, -2):
                    j = i
                    while (j + 1 < len(rows) and len(rows[j + 1][1]) == 3 and rows[j + 1][1][0] == key[0]
                           and rows[j + 1][0] == rows[j][0] + 1
                           and rows[j + 1][1][1] <= rows[j][1][1] + step <= rows[j + 1][1][2]
                           and (rows[j + 2][0] - 1 if j + 2 < len(rows) else last) == rows[j + 1][0]):
                        j += 1
                    if j > i + 2: break
                else:
                    j = i
            if j > i:
                print(f'{fr:6d}-{rows[j][0]:6d} ({rows[j][0] - fr + 1:4d} frames) pos {o:02x}:{r:02x} drawn {drawn}: '
                      f'file {key[0]} ramp {key[1]}..{key[2]} -> {rows[j][1][1]}..{rows[j][1][2]} step {step}')
                i = j + 1
            else:
                print(f'{fr:6d}-{nxt:6d} ({nxt - fr + 1:4d} frames) pos {o:02x}:{r:02x} drawn {drawn}: {key}')
                i += 1
