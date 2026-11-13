"""Closed forms of the dt-integrated state of parts 2, 4, 5 and 11, checked against dumped frames.

Every frame adds dt = t70(T_now) - t70(T_prev) (t70 = timer * 70 / 1193182, T = the timer read after each shown
frame). A part (or an event) that starts in frame k therefore integrates from tau = the timer of the frame before:
the last frame timer below the row start (66300 * row - 22100 PIT clocks). See event_lag.py.

usage: python3 models/grid/clocks.py PART DUMP.pkl FLIPS [FLIPS ...]   (from clone/re)
  e.g. clocks.py 5 dump/grid/p5.pkl full.flips
       clocks.py 5 dump/grid/p5slow.pkl full.flips dump/grid/p5slow.flips   (resumed run: earlier frames from full)
Prints the largest difference between the dumped state and the closed form (in the variable's own units).
"""
import bisect
import os
import pickle
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gridparts as gp  # noqa: E402

PIT = 1193182


def t70(x): return x * 70.0 / PIT


def load_timers(paths):
    """frame timers in order: the frames of a resumed run replace the full run's frames from its first number."""
    T = {}
    for p in paths:
        new = {}
        for line in open(p):
            if line.startswith('#'): continue
            f = line.split()
            if int(f[3]) > -(1 << 39): new[int(f[0])] = int(f[3])
        if new:
            T = {k: v for k, v in T.items() if k < min(new)}
        T.update(new)
    return [T[k] for k in sorted(T)]


def tau(T, order, row):
    pt = 66300 * (order * 64 + row) - 22100
    return T[bisect.bisect_left(T, pt) - 1]


def piecewise(t, t0, switches, rates):
    """integral of a piecewise constant rate from t0 to t (rates[i] after switches[i-1])."""
    s, prev, i = 0.0, t0, 0
    for q in switches + [None]:
        end = t if q is None or q > t else q
        s += rates[i] * (t70(end) - t70(prev))
        prev = end
        if q is None or q >= t: break
        i += 1
    return s


def main():
    part, dump, flips = int(sys.argv[1]), sys.argv[2], sys.argv[3:]
    T = load_timers(flips)
    recs = [r for r in pickle.load(open(dump, 'rb')) if r['part'] == part]
    worst = {}

    def err(name, got, want):
        worst[name] = max(worst.get(name, 0.0), abs(got - want))

    for rec in recs:
        f, tm = rec['fill'] if 'fill' in rec else rec['exit'], rec['timer']
        if part == 5:
            t0 = tau(T, 0x20, 0)
            flipsw = [tau(T, o, 0) for o in (0x21, 0x22, 0x23)]
            Tc = 0.005 * (t70(tm) - t70(t0))
            Rc = 0.01 * piecewise(tm, t0, flipsw, [1, -1, 1, -1])
            err('T', gp.F(f, 0xea6be4), Tc)
            err('R', gp.F(f, 0xea6be8), Rc)
        elif part == 2:
            t0, t201 = tau(T, 0x0c, 0), tau(T, 0x0e, 0)
            TC = t70(tm) - t70(t0)
            after = tm > t201
            Br = min(1.0, 0.3 + 0.01 * (t70(tm) - t70(t201 if after else t0)))
            err('TC', gp.F(f, 0xf73928), TC)
            err('Br', gp.F(f, 0x428160), Br)
        elif part == 4:
            t0, t403 = tau(T, 0x30, 0x20), tau(T, 0x31, 0)
            sw = [tau(T, o, 0) for o in (0x32, 0x33, 0x34, 0x35)]
            T4 = t70(tm) - t70(t0)
            T4o = t70(T[T.index(tm) - 1]) - t70(t0)
            B = min(1.0, 0.01 * (t70(tm) - t70(t403))) if tm > t403 else 0.5 + 0.3 * np.sin(0.07 * T4o)
            rates = [(0.01, 0.012, 0.008), (0.004, -0.01, 0.01), (-0.013, 0.005, -0.009)]
            a = [piecewise(tm, t0, sw, [rates[k % 3][i] for k in range(5)]) for i in range(3)]
            err('T4', gp.F(f, 0x421a8c), T4)
            err('B', gp.F(f, 0x421a88), B)
            for i, adr in enumerate((0x421a90, 0x421a94, 0x421a98)):
                err('a%d' % (i + 1), gp.F(f, adr), a[i])
        elif part == 11:
            t0, t1101 = tau(T, 0x08, 0x20), tau(T, 0x0b, 0)
            err('clock', gp.F(f, 0xf73934), 0.05 * (t70(tm) - t70(t0)))
            err('fade', gp.F(f, 0x428464), max(0.0, 1 - 0.0065 * (t70(tm) - t70(t1101))) if tm > t1101 else 1.0)
    print('part %d, %d frames: max |dumped - closed form|: %s' % (
        part, len(recs), ', '.join('%s %.3g' % kv for kv in worst.items())))


if __name__ == '__main__':
    main()
