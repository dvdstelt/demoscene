"""Frame rates of the 3D entries in the capture (full.ticks: the drawn-frame counter per refresh) and the border the
AA pass leaves unwritten.

usage: python3 rates.py
"""
import os
import sys

import numpy as np

import engine

sys.path.insert(0, engine.RE)
from sheet import open_frames, indexed, read_ticks  # noqa: E402

ENTRIES = [(1, 'INTRO', 652, 1327), (2, 'INTRO', 1327, 1953), (3, 'INTRO', 1953, 2371), (4, 'INTRO', 2371, 2736),
           (8, 'P3D1', 4097, 6328), (11, 'PART2', 7230, 8119), (14, 'P3D3', 9915, 10806),
           (22, 'PART4', 14492, 15377), (26, 'FIN', 16276, 16720)]


def main():
    t = read_ticks(os.path.join(engine.RE, 'full.ticks'))
    raw, n = open_frames(os.path.join(engine.RE, 'full.frames'))
    for e, name, a, b in ENTRIES:
        d = [int(t[k]['drawn']) for k in range(a, b + 1)]
        frames = d[-1] - d[0]
        gaps = np.diff([k for k in range(a, b) if d[k - a + 1] != d[k - a]])
        hist = np.bincount(gaps)
        print('entry %2d %-5s refreshes %4d..%4d: %4d frames, %.1f fps (%.2f per refresh); refreshes per frame: %s'
              % (e, name, a, b, frames, frames * 70.086 / (b - a), frames / (b - a),
                 {i: int(c) for i, c in enumerate(hist) if c}))
        px = indexed(raw, (a + b) // 2)[0]
        border = np.concatenate([px[0], px[199], px[:, :4].ravel(), px[:, 316:].ravel()])
        print('          border (rows 0, 199, columns 0..3, 316..319) nonzero pixels at mid-part: %d' %
              np.count_nonzero(border))


if __name__ == '__main__':
    main()
