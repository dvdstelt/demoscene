"""Find frame-rate dependent state: compare two run_jizz.py runs made at different --budget (CPU speeds) from the
same snapshot, frame by frame at equal intro time.

For every N-th tick record of run B, take the frame it shows (the last page flipped, .ticks column 9), look up the
xmpGetTimer value that frame was drawn with (.flips), find the frame of run A drawn at the nearest timer value that
is visible in a tick record of A, and print the mean absolute RGB difference of the two pictures. A part whose
pictures depend only on the timer gives ~0 (up to the timer mismatch, printed in ms); per-frame state (counters
stepped once per drawn frame) shows up as differences that grow over the part.

usage: python3 fpscheck.py A_prefix B_prefix [--every 30] [--tick0 T | --tick0a TA --tick0b TB] [--part 17003]
       (A_prefix.frames/.ticks/.flips; --tick0 = the tick of the first record, i.e. of the snapshot a run was
       resumed from; 0 for a run from the start)
"""
import argparse
import bisect

import numpy as np

from sheet import open_frames, to_rgb

PIT_HZ = 1193182
NONE = -(1 << 40)            # no timer read in that frame


def load(prefix):
    flips = {}
    for line in open(prefix + '.flips'):
        if line.startswith('#'): continue
        f = line.split()
        flips[int(f[0])] = int(f[3])
    ticks = []
    for line in open(prefix + '.ticks'):
        if line.startswith('#'): continue
        f = line.split()
        ticks.append((int(f[0]), int(f[8]), f[7], float(f[2])))
    return flips, ticks, open_frames(prefix + '.frames')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--every', type=int, default=30)
    ap.add_argument('--tick0', type=int, default=0, help='tick of the first record of both runs')
    ap.add_argument('--tick0a', type=int, help='tick of the first record of run A (default --tick0)')
    ap.add_argument('--tick0b', type=int, help='tick of the first record of run B (default --tick0)')
    ap.add_argument('--part', help='only tick records of this part (hex, as in .ticks)')
    ap.add_argument('--maxdt', type=float, help='only pairs whose timer values differ by at most this many ms')
    a = ap.parse_args()
    t0a = a.tick0 if a.tick0a is None else a.tick0a
    t0b = a.tick0 if a.tick0b is None else a.tick0b
    fa, ta, FA = load(a.a)
    fb, tb, FB = load(a.b)
    # visible A frames: flip number -> first tick record showing it
    vis = {}
    for t, fl, part, tm in ta:
        vis.setdefault(fl, t)
    va = sorted((fa[f], f) for f in vis if f in fa and fa[f] != NONE)
    keys = [v[0] for v in va]

    def frame(F, t, t0):
        return to_rgb(F[0], t - t0, *F[1:6]).astype(np.int32)

    print('  tickB  part    timeB(s)  dt(ms)  diff(A,B)  motion(A)')
    for t, fl, part, tm in tb[::a.every]:
        if fl not in fb or fb[fl] == NONE or not keys or (a.part and part != a.part): continue
        timer = fb[fl]
        i = bisect.bisect_left(keys, timer)
        cands = [j for j in (i - 1, i) if 0 <= j < len(keys)]
        j = min(cands, key=lambda j: abs(keys[j] - timer))
        if a.maxdt is not None and abs(keys[j] - timer) * 1000 / PIT_HZ > a.maxdt: continue
        ta_ = vis[va[j][1]]
        img_a, img_b = frame(FA, ta_, t0a), frame(FB, t, t0b)
        prev = vis.get(va[j - 1][1]) if j > 0 else None
        motion = np.abs(img_a - frame(FA, prev, t0a)).mean() if prev is not None else float('nan')
        print('%7d %6s %10.3f %7.1f %10.2f %10.2f' % (t, part, timer / PIT_HZ, (keys[j] - timer) * 1000 / PIT_HZ,
                                                      np.abs(img_a - img_b).mean(), motion))


if __name__ == '__main__':
    main()
