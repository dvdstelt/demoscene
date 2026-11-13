"""Compare the page drawn in a dump (run_engine.py --dump-draws), and the page engine.py draws from it, with the
frame of the reference capture (full.frames) that shows it.

usage: python3 capture_check.py DUMP.npz [...]
The dump's frame is flip number flips + 1; full.ticks column 9 says which flip is on screen at the end of each tick.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))


def main():
    ticks = np.loadtxt(os.path.join(RE, 'full.ticks'), comments='#', usecols=(0, 8), dtype=np.int64)
    frames = np.memmap(os.path.join(RE, 'full.frames'), np.uint8, 'r', 32)
    for path in sys.argv[1:]:
        d = np.load(path)
        flip = int(d['flips']) + 1
        t = ticks[ticks[:, 1] == flip, 0]
        if not len(t):
            print('%s: flip %d is not on screen at the end of any tick' % (os.path.basename(path), flip))
            continue
        cap = np.asarray(frames[t[0] * 192000:(t[0] + 1) * 192000]).reshape(200, 320, 3)
        after = d['after'].view('<u4').reshape(200, 320)
        rgb = np.stack([(after >> 16) & 255, (after >> 8) & 255, after & 255], -1).astype(np.uint8)
        diff = (rgb != cap).any(-1)
        msg = '%s: flip %d on screen at tick %d; drawn page vs capture: %d of 64000 pixels differ' % (
            os.path.basename(path), flip, t[0], diff.sum())
        mp = path.replace('.npz', '_model.npy')
        if os.path.exists(mp):
            m = np.load(mp).reshape(200, 320)
            mrgb = np.stack([(m >> 16) & 255, (m >> 8) & 255, m & 255], -1).astype(np.uint8)
            msg += '; model vs capture: %d differ' % (mrgb != cap).any(-1).sum()
        print(msg)


if __name__ == '__main__':
    main()
