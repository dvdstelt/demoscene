"""Check the bitmap model (bitmap.py) against a capture: every record of every RunBITMAPEFEK line.

usage: python3 models/bitmap/verify.py full     (or part2)    [--every N]

The offsets start at zero (ClearEFEKPARAMS zeroes them too) and are stepped by the frame counter (0x40c22c once per
refresh) during the runs only. A run's last step is at the counter of the first record that shows the next script
line; a run that follows a run starts there; a run after Init starts at the counter of the record before or of its
first record (both are tried on its records 2..5, the better one is kept). A record with counter F shows the draw
for F in its top rows and the draw for F - 1 below (the drawer writes straight into VGA memory and is part-way down
the screen when the record is taken): every pixel must equal one of the two and the new pixels must come first in
drawing order. Before a run's first draw the screen holds the previous run's last draw (or zeros after ClearSCR); the
first record after an Init may show the first draw still in progress over it. Prints per run: t0, refreshes lost
since the previous run, end, steps, records checked and exact, the split rows.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bitmap import Effect, read_prm, step  # noqa: E402

RE = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, RE)
from sheet import open_frames, indexed, read_ticks  # noqa: E402


def script_lines(path):
    out = []
    for line in open(path):
        if line.startswith('#'):
            continue
        f = line.split(None, 4)
        out.append((int(f[0]), int(f[2]), int(f[3]), f[4].strip()))
    return out


def order_index(eff):
    """drawing order of the 64,000 pixels (1x1: row by row; 2x2: block rows, each covers two screen rows)"""
    if eff.type == 1:
        return np.arange(64000).reshape(200, 320)
    o = np.zeros((200, 320), np.int64)
    for by in range(100):
        for bx in range(160):
            i = by * 160 + bx
            o[2 * by, 2 * bx:2 * bx + 2] = 4 * i + np.array([0, 1])
            o[2 * by + 1, 2 * bx:2 * bx + 2] = 4 * i + np.array([2, 3])
    return o


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cap')
    ap.add_argument('--every', type=int, default=1)
    a = ap.parse_args()
    raw, nrec = open_frames(os.path.join(RE, a.cap + '.frames'))
    ticks = read_ticks(os.path.join(RE, a.cap + '.ticks'))
    lines = script_lines(os.path.join(RE, a.cap + '.script'))
    first = {}
    for k in sorted(ticks):
        first.setdefault((ticks[k].part, ticks[k].line), k)
    offs = [0] * 8
    eff = None
    order = None
    total = [0, 0]
    prev_end = None
    screen = np.zeros((200, 320), np.uint8)   # what VGA memory holds before a run's first draw
    for i, (frame, part, ln, text) in enumerate(lines):
        w = text.split()
        cmd = w[2]
        if cmd == 'ClearSCR':
            screen = np.zeros((200, 320), np.uint8)
        if cmd == 'ClearEFEKPARAMS':
            offs = [0] * 8
            prev_end = None
        if cmd == 'InitBITMAPEFEK':
            eff = Effect(w[3], part)
            order = order_index(eff)
        if cmd != 'RunBITMAPEFEK':
            continue
        sp = read_prm(w[3], part)
        k0 = first[(part, ln)]
        k1 = next((k for k in range(k0, nrec) if k in ticks and (ticks[k].part, ticks[k].line) != (part, ln)), nrec)
        # the run's last step is at the counter of the record that first shows the next line (the loop leaves
        # right after a counter increment, before that refresh's record)
        t_end = ticks[k1].frames if k1 < nrec else ticks[nrec - 1].frames
        # start: the counter when the line started; the line starts either before or after the increment that
        # precedes record k0: try both and keep the one that reproduces the first frames
        prev_cmd = lines[i - 1][3].split()[2] if i > 0 else ''
        if prev_cmd == 'RunBITMAPEFEK' and prev_end is not None:
            t0 = prev_end             # consecutive runs: no refresh is lost
        else:
            best = None
            for t0c in (ticks[k0 - 1].frames, ticks[k0].frames):
                sc = 0
                for k in range(k0 + 1, min(k0 + 5, k1)):
                    F = ticks[k].frames
                    px = indexed(raw, k)[0]
                    a_ = px == eff.draw(step(offs, sp, F - t0c))
                    b_ = px == eff.draw(step(offs, sp, F - 1 - t0c))
                    sc += int((a_ | b_).sum())
                if best is None or sc > best[0]:
                    best = (sc, t0c)
            t0 = best[1]
        lost = t0 - prev_end if prev_end is not None else None
        o0 = list(offs)
        ok = 0
        cnt = 0
        splits = []
        bad = []
        cache = {}

        def frame_for(F):
            if F not in cache:
                cache[F] = eff.draw(step(o0, sp, F - t0) if F >= t0 else o0)
            return cache[F]
        def check(px, new, old):
            isnew = px == new
            isold = px == old
            neither = ~(isnew | isold)
            if (new == old).all():
                return not neither.any(), None, int(neither.sum())
            dif = new != old
            o_new = order[isnew & dif]
            o_old = order[isold & dif & ~isnew]
            split = (o_new.max() if o_new.size else -1)       # new pixels must come first in drawing order
            good = (not neither.any()) and (o_old.size == 0 or o_old.min() > split)
            return good, split, int(neither.sum())
        for k in range(k0, k1, a.every):
            F = ticks[k].frames
            px, pal = indexed(raw, k)
            good, split, nbad = check(px, frame_for(max(F, t0)), frame_for(F - 1) if F - 1 >= t0 else screen)
            if not good and k == k0 and F - 1 >= t0:
                # the first draw after an Init can start late: the record shows it in progress over the old screen
                good, split, nbad = check(px, frame_for(F - 1), screen)
            cnt += 1
            ok += good
            if split is not None and split >= 0:
                splits.append(split // 320 if eff.type == 1 else (split // 4) // 160 * 2)
            if not good and len(bad) < 5:
                bad.append((k, F, nbad))
        total[0] += cnt
        total[1] += ok
        offs = step(o0, sp, t_end - t0)
        prev_end = t_end
        screen = frame_for(t_end - 1)
        print('line %d %-20s t0 %6d (lost %s) end %6d steps %4d frames %4d exact %4d  split rows %s  bad %s' % (
            ln, w[3], t0, lost, t_end, t_end - t0, cnt, ok,
            (min(splits), max(splits)) if splits else None, bad), flush=True)
    print('total frames %d exact %d' % tuple(total))


if __name__ == '__main__':
    main()
