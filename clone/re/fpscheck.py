"""Find frame-rate dependent state: compare two run_clone.py runs made at different --ips (CPU speeds), frame by
frame at equal intro time.

For every N-th tick record of run B, take the frame it shows (the last frame shown, .ticks column 9), look up the
timer value that frame was drawn with (.flips column 4), find the frame of run A drawn at the nearest timer value
that is visible in a tick record of A, and print the mean absolute RGB difference of the two pictures. A part whose
pictures depend only on the timer gives ~0 (up to the timer mismatch, printed in ms); per-frame state (anything
stepped once per drawn frame) shows up as differences that grow over the part. motion(A) = the difference between
that frame of A and the previous visible one, for scale.

usage: python3 fpscheck.py A_prefix B_prefix [--every 30] [--tick0 T | --tick0a TA --tick0b TB] [--part 7]
       (A_prefix.frames/.ticks/.flips, per-tick captures; --tick0 = the tick of the first record, i.e. of the
       snapshot a run was resumed from; 0 for a run from the start)
"""
import argparse
import bisect

import numpy as np

from sheet import open_frames, to_rgb, read_flips, read_ticks

PIT_HZ = 1193182
NONE = -(1 << 40)


def load(prefix):
    flips, _ = read_flips(prefix + '.flips')
    ticks = read_ticks(prefix + '.ticks')
    return {f: r[2] for f, r in flips.items()}, ticks, open_frames(prefix + '.frames')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--every', type=int, default=30)
    ap.add_argument('--tick0', type=int, default=0)
    ap.add_argument('--tick0a', type=int)
    ap.add_argument('--tick0b', type=int)
    ap.add_argument('--part', type=int, help='only tick records of this part')
    ap.add_argument('--maxdt', type=float, help='only pairs whose timer values differ by at most this many ms')
    a = ap.parse_args()
    t0a = a.tick0 if a.tick0a is None else a.tick0a
    t0b = a.tick0 if a.tick0b is None else a.tick0b
    fa, ta, FA = load(a.a)
    fb, tb, FB = load(a.b)
    vis = {}                                    # A: flip -> first tick record showing it
    for t in sorted(ta):
        vis.setdefault(ta[t][5], t)
    va = sorted((fa[f], f) for f in vis if f in fa and fa[f] != NONE)
    keys = [v[0] for v in va]

    def frame(F, t, t0):
        raw, w, h, rec, hdr, n, _ = F
        return to_rgb(raw, t - t0, w, h, rec, hdr).astype(np.int32)

    print('  tickB part    timeB(s)  dt(ms)  diff(A,B)  motion(A)')
    for t in sorted(tb)[::a.every]:
        tm, order, row, timer, part, fl = tb[t]
        if fl not in fb or fb[fl] == NONE or not keys or (a.part is not None and part != a.part): continue
        if not 0 <= t - t0b < FB[5]: continue
        timer = fb[fl]
        i = bisect.bisect_left(keys, timer)
        j = min((j for j in (i - 1, i) if 0 <= j < len(keys)), key=lambda j: abs(keys[j] - timer))
        if a.maxdt is not None and abs(keys[j] - timer) * 1000 / PIT_HZ > a.maxdt: continue
        ta_ = vis[va[j][1]]
        if not 0 <= ta_ - t0a < FA[5]: continue
        img_a, img_b = frame(FA, ta_, t0a), frame(FB, t, t0b)
        prev = vis.get(va[j - 1][1]) if j > 0 else None
        motion = np.abs(img_a - frame(FA, prev, t0a)).mean() if prev is not None and 0 <= prev - t0a < FA[5] \
            else float('nan')
        print('%7d %4d %10.3f %7.1f %10.2f %10.2f' % (t, part, timer / PIT_HZ, (keys[j] - timer) * 1000 / PIT_HZ,
                                                     np.abs(img_a - img_b).mean(), motion))


if __name__ == '__main__':
    main()
