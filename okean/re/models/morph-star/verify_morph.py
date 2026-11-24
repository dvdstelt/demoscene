"""Compare morph.py with the capture: every record of the part (or a sample), using the callback count and the
drawn-frame number from hookrun.py's log (p4_hooks.log).

usage: python3 verify_morph.py [--every N] [--from R] [--to R]
"""
import argparse

import numpy as np

import hooks
import morph
from common import open_capture, capture_frame, rdbytes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--every', type=int, default=1)
    ap.add_argument('--from', dest='r0', type=int, default=0)
    ap.add_argument('--to', dest='r1', type=int, default=1 << 30)
    a = ap.parse_args()
    h = [x for x in hooks.parse('p4_hooks.log') if a.r0 <= x['record'] <= a.r1]
    h = h[::a.every]
    seeds, _ = morph.noise_seeds(max(x['n'] for x in h) + 1)
    cap = open_capture()
    pal0 = np.frombuffer(rdbytes(0x35db0, 768), np.uint8).reshape(256, 3).astype(np.int64)
    bad = 0
    for x in h:
        assert (seeds[x['n']] - x['vars']['seed']) % (1 << 32) == 0, x
        img = morph.frame(x['cbs'], seeds[x['n']])
        ref, pal = capture_frame(cap, x['record'])
        d = int((img != ref).sum())
        fade = x['vars_copy']['fade']             # read after the retrace wait, after the callback
        order = int(x['pos_copy'].split(':')[0])
        # the palette the main loop writes in order 15 (pal * fade >> 6), otherwise the one of the set-up
        want = (pal0 * fade) >> 6 if order == 15 else pal0
        pd = int((want != pal).sum())
        if d or pd:
            bad += 1
            print('record %d (c %d, n %d, pos %s): %d pixels differ, %d palette bytes differ' % (
                x['record'], x['cbs'], x['n'], x['pos'], d, pd), flush=True)
    print('%d records checked, %d differ' % (len(h), bad))


if __name__ == '__main__':
    main()
