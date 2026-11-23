"""Model of the twirl's frame (0x40a264 = 0x40a5a4 ring table + 0x40a54c pixels) and tick (0x40a20c), checked
against full.frames.

State (floats, all 0 at program start, never reset): s58 (angle scroll, columns), s2c (radius scroll, rows),
p1 = [0x4b6d50] (phase of the radial wave), p2 = [0x4b6d34] (phase of the angular wave).  Per refresh (0x40a20c):
s58 += [0x4b6d08], s2c += [0x4b6d0c], p1 += [0x4b6d20], p2 += [0x4b6d1c] (float32 stores).
Per frame: ring table T[k] (k = 0..255) = 256 trunc(s2c) + trunc(s58) + 256 trunc(A sin(p1 + k d18))
+ trunc(B sin(p2 + k d14))  (16-bit), A = [0x4b6d10], B = [0x4b6d24], d18 = [0x4b6d18], d14 = [0x4b6d14];
pixel = pic[0x8000 + (int16)(T[v & 0xff] + ((v << 8) + (u & 0xff)))] with (u, v) from the pre-table.

usage: frame.py [first last]   for every capture record in the range, find the number of tick steps n whose state
renders the record's pixels exactly (prints n - (k - 10574), the lag between the record and the state it shows).
"""
import math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)
import datfile
from sheet import open_frames, indexed

f32 = np.float32
SETS = [  # +0 d18, +4 p1 speed, +8 A, +c s58 speed, +10 s2c speed, +14 p2 speed, +18 B, +1c d14 (0x409ea0)
    (0.02, 0.12, 45.0, 1.0, 0.0, 0.0, 0.0, 0.0),
    (0.0, -0.04, 99.0, 0.0, 0.0, 0.04, 98.0, 0.03),
    (0.0, 0.0, 0.0, 0.0, -2.0, 0.02, 189.0, 0.0),
    (0.42, -0.1, 13.0, 0.0, -2.0, 0.0, 0.0, 0.0),
    (0.34, 0.08, 12.0, 0.0, 3.0, 0.0, 0.0, 0.0),
]
SETS = [tuple(float(f32(x)) for x in s) for s in SETS]
PICS = [('flower/jeesus1.raw', 'flower/jeesus.pal'), ('nigga/nigga2.raw', 'nigga/nigga2.pal'),
        ('nigga/nigga4.raw', 'nigga/nigga4.pal'), ('flower/jeesus2.raw', 'flower/jeesus.pal'),
        ('flight/river.raw', 'flight/map.pal')]

dat = datfile.Dat(os.path.join(RE, '..', 'original', 'INSIDE.DAT'))
_p = np.load(os.path.join(HERE, 'pre.npz')); u, v = _p['u'], _p['v']
W0 = (v & 0xff).astype(np.int64)
W1 = (((v << 8) + (u & 0xff)) & 0xffff).astype(np.int64)


def pic(k):
    return np.frombuffer(bytes(dat.get(PICS[k][0])), np.uint8)


def ring_table(st, ps):
    d18, _, A, _, _, _, B, d14 = ps
    s58, s2c, p1, p2 = st
    base = 256 * int(s2c) + int(s58)
    T = np.zeros(256, np.int64)
    a, b = p1, p2                      # accumulated in 80-bit: plain double sums are close enough
    for k in range(256):
        T[k] = (base + 256 * int(A * math.sin(a)) + int(B * math.sin(b))) & 0xffff
        a += d18; b += d14
    return T


def render(st, ps, picture):
    T = ring_table(st, ps)
    w = (T[W0] + W1) & 0xffff
    off = (w ^ 0x8000)                 # 0x8000 + (int16) w
    return picture[off].reshape(200, 320)


def step(st, ps):
    s58, s2c, p1, p2 = st
    return (float(f32(s58 + ps[3])), float(f32(s2c + ps[4])), float(f32(p1 + ps[1])), float(f32(p2 + ps[5])))


def schedule(event_refreshes):
    """States after n tick steps (n = 0..N) and the set / picture in force, given the tick-step numbers at which
    the events took effect (an event before step m changes the speeds used by step m)."""
    st = (0.0, 0.0, 0.0, 0.0)
    states = [st]
    sets = [1]
    cur = 1
    for n in range(1, 760):
        for e, s in event_refreshes:
            if e == n:
                cur = s
        st = step(st, SETS[cur])
        states.append(st)
        sets.append(cur)
    return states, sets


def main():
    first = int(sys.argv[1]) if len(sys.argv) > 1 else 10575
    last = int(sys.argv[2]) if len(sys.argv) > 2 else 11275
    raw, _ = open_frames(os.path.join(RE, 'full.frames'))
    # Tick step at which each event's speeds first apply (found by events2.py / this script): the script index
    # first shows the event done at records 10711, 10843, 11128; the events ran just after (30:24) or just before
    # (30:48 + 30:12, 31:36) that refresh's tick step.  30:48 (set 3, nigga4) and 30:12 (set 4, jeesus2) ran in the
    # same refresh, so set 3 never steps and nigga4 is never drawn.
    ev = [(138, 2), (269, 3), (269, 4), (int(os.environ.get('E0', 554)), 0)]
    states, sets = schedule(ev)
    picks = {1: 0, 2: 1, 3: 2, 4: 3, 0: 4}
    for k in range(first, last + 1):
        px, _ = indexed(raw, k)
        n0 = k - 10574
        found = []
        for n in range(max(0, n0 - 4), min(len(states), n0 + 3)):
            for s in {sets[n], sets[min(n + 1, len(sets) - 1)]}:
                img = render(states[n], SETS[s], pic(picks[s]))
                d = int((img != px).sum())
                found.append((d, n - n0, s))
        found.sort()
        print(k, 'best (diff pixels, n - (k-10574), set):', found[:2])


if __name__ == '__main__':
    main()
