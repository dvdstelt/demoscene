"""Compare star.py with the capture: every record of the part (or a sample), using the callback count and the
drawn-frame number from hookrun.py's log (p6_hooks.log). The caption frames (the text printer) are reported apart.

usage: python3 verify_star.py [--every N] [--from R] [--to R]
"""
import argparse

import numpy as np

import hooks
import star
from common import open_capture, capture_frame, rdbytes, caption


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--every', type=int, default=1)
    ap.add_argument('--from', dest='r0', type=int, default=0)
    ap.add_argument('--to', dest='r1', type=int, default=1 << 30)
    a = ap.parse_args()
    h = [x for x in hooks.parse('p6_hooks.log') if a.r0 <= x['record'] <= a.r1]
    h = h[::a.every]
    seeds = star.noise_seeds(max(x['n'] for x in h) + 1)
    cap = open_capture()
    pal0 = np.frombuffer(rdbytes(0x3bb24, 768), np.uint8).reshape(256, 3).astype(np.int64)
    bad = 0
    ncap = [0]
    for x in h:
        assert (seeds[x['n']] - x['vars']['seed']) % (1 << 32) == 0, x
        img = star.frame(x['cbs'], seeds[x['n']])
        # the printer's caption segment is current while it sees order 22 rows 0..15 (it advances when it sees
        # the change, so it shows one frame late; the drawing starts about one frame before the printer runs)
        order, row = (int(v) for v in x['pos'].split(':'))
        if order == 22 and row < 16:
            caption(img, 'the sea robot of love')
            ncap[0] += 1
        ref, pal = capture_frame(cap, x['record'])
        d = int((img != ref).sum())
        # palette: 0x3c128 runs before the retrace wait: the fade after the callbacks the drawing started with
        pd = int((((pal0 * x['vars']['fade']) >> 4) != pal).sum())
        if d or pd:
            bad += 1
            print('record %d (c %d, n %d, pos %s, cbs at copy %d): %d pixels differ, %d palette bytes differ' % (
                x['record'], x['cbs'], x['n'], x['pos'], x['cbs_copy'], d, pd), flush=True)
    print('%d records checked (%d with the caption), %d differ' % (len(h), ncap[0], bad))


if __name__ == '__main__':
    main()
