"""Part 2's frame-rate dependence: the camera position is computed from the camera clock before this frame's dt
is added, the rotation from the clock after it. Renders part 2 (per-pixel model, 320 x 160) at the camera clocks of
the dumped frames with the position lagging by one frame interval at 15, 41 (capture) and 84 fps, and prints the
mean absolute RGB difference to the capture-rate picture (PARTS.md measured 13 at 15 fps and 4.7 at 84 fps).

usage: python3 models/grid/lag_effect.py   (from clone/re)
"""
import os
import pickle
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import filler  # noqa: E402
import perpixel as pp  # noqa: E402

RE = os.path.normpath(os.path.join(HERE, '..', '..'))


def main():
    T, W, tex = filler.tables(os.path.join(RE, 'snaps', 't2316.snap'))
    recs = [r for r in pickle.load(open(os.path.join(RE, 'dump', 'grid', 'p2.pkl'), 'rb')) if r['part'] == 2]
    py, px = np.mgrid[20:180, 0:320].astype(np.float64)
    res = {15: [], 84: [], 'none': []}
    for rec in recs[::4]:
        f = rec['fill']
        TC, Br, flag = pp.F(f, 0xf73928), pp.F(f, 0x428160), pp.I(f, 0x42815c)

        def img(lag):
            return pp.shade(*pp.part2(px, py, TC - lag, TC, Br, flag), tex)
        ref = img(70 / 41.0)
        for fps in (15, 84):
            res[fps].append(np.abs(img(70.0 / fps) - ref).mean())
        res['none'].append(np.abs(img(0.0) - ref).mean())
    for k, v in res.items():
        print('position lag of one frame at %s fps vs the capture (41 fps): mean |diff| median %.2f, max %.2f' % (
            k, np.median(v), max(v)))


if __name__ == '__main__':
    main()
