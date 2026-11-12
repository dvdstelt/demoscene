"""p6 / p7 script models: the per-step parameters of the main scripts (p6 3951, p7 31ca) and the per-retrace
integration (p6 1cc0, p7 1f40), checked against snapshots (state after R retraces of step S; R = [1736] / [199a])."""
import sys
from snap import Snap

# p6: (N retraces, changes applied before the step). Variables: vel = group velocity (1632/1636/163a),
# avel = group angular velocity (1644/1646/1648), lab = label direction [176a], seg = label picture [176c],
# bump = [1770] (bump strength step), morph = [325b] (palette morph step)
P6 = [
    (140, dict(vel=(8000, 0, -8600), avel=(-4, 128, 0))),
    (140, dict(lab=1)),
    (140, dict(vel=(5000, 0, 0))),
    (280, dict(vel=(0, 0, 6000))),
    (210, dict(vel=(0, 0, 0))),
    (490, dict(avel=(-14, 128, 0), vel=(0, 0, -5000))),
    (280, dict(avel=(14, 128, 0), vel=(0, 0, 0))),
    (210, dict(lab=-1, avel=(-4, 128, 0), vel=(0, 0, 6000))),
    (210, dict(seg=0x3494, lab=1)),
    (210, dict(avel=(16, 128, 0), vel=(0, 0, -3000))),
    (420, dict(bump=-4, morph=16, vel=(0, 0, -4000), avel=(-8, 128, 0))),
    (280, dict(vel=(0, 0, 400), avel=(0, 128, 0), bump=6, morph=-24)),
    (280, dict(vel=(0, 0, 0), bump=-6, morph=24)),
    (140, dict(avel=(-4, 128, 0), lab=-1)),
    (280, dict(avel=(8, 128, 0), seg=0x358f, lab=1)),
    (280, dict(lab=-1, vel=(6000, 0, -2600))),
]
P6_INIT = dict(pos=[-0x2ee000, 0, 0x4a3800], ang=[0, 0, 0], cat_ang=[0, 0, 0], cat_avel=(0, -180, 0),
               bumpk=0x100, m=0, w=0)

P7 = [
    (210, dict(vel=(0, -4000, 0), avel=(0, 0, 48))),
    (490, dict(vel=(0, -2000, 0), lab=1)),
    (70, dict(vel=(0, -1200, -3000), avel=(-18, 7, 58), lab=-1)),
    (140, dict(vel=(700, -500, -3000))),
    (420, dict(avel=(-18, 7, 68), seg=0x3ce6, lab=1)),
    (560, dict(vel=(2400, 0, 0), avel=(-18, 7, 78))),
    (210, dict(lab=-1)),
]
P7_INIT = dict(pos=[0, 0x232800, 0x36b000], ang=[0, 0, 0], w=-1)


def s16(x): return ((x + 0x8000) & 0xffff) - 0x8000


def run(script, init, step, r, part):
    """state after r retraces of step `step` (0-based), stepping like the per-retrace routine"""
    st = {k: (list(v) if isinstance(v, list) else v) for k, v in init.items()}
    vel, avel, lab, bumpd, morphd = (0, 0, 0), (0, 0, 0), 0, 0, 0
    for k in range(step + 1):
        n, ch = script[k]
        vel = ch.get('vel', vel); avel = ch.get('avel', avel); lab = ch.get('lab', lab)
        bumpd = ch.get('bump', bumpd); morphd = ch.get('morph', morphd)
        for _ in range(n if k < step else r):
            # label slide (1cc0 / 1f40): stop when leaving [0, 0x4b)
            if lab:
                st['w'] += lab
                if not (0 <= st['w'] < 0x4b):
                    lab = 0
            if part == 6:
                if morphd:
                    st['m'] += morphd
                    if not (0 <= st['m'] < 0x1000):
                        morphd = 0
                if bumpd:
                    st['bumpk'] += bumpd
                    if not (0 <= st['bumpk'] < 0x100):
                        st['bumpk'] -= bumpd
                        bumpd = 0
                st['cat_ang'] = [(a + v) & 0xffff for a, v in zip(st['cat_ang'], st['cat_avel'])]
            st['ang'] = [(a + v) & 0xffff for a, v in zip(st['ang'], avel)]
            st['pos'] = [p + v for p, v in zip(st['pos'], vel)]
    return st


def check(path, part, step):
    s = Snap(path)
    if part == 6:
        r = s.cs32(0x1736)
        st = run(P6, P6_INIT, step, r, 6)
        got = dict(pos=[s.cs32(0x1626 + 4 * k) for k in range(3)], ang=[s.cs16(0x163e + 2 * k) for k in range(3)],
                   cat_ang=[s.cs16(0x165a + 0x1e + 2 * k) for k in range(3)], bumpk=s.cs16(0x176e),
                   m=s16(s.cs16(0x3259)), w=s16(s.cs16(0x1768)))
    else:
        r = s.cs32(0x199a)
        st = run(P7, P7_INIT, step, r, 7)
        got = dict(pos=[s.cs32(0x1816 + 4 * k) for k in range(3)], ang=[s.cs16(0x182e + 2 * k) for k in range(3)],
                   w=s16(s.cs16(0x19c8)))
    bad = {k: (st[k], got[k]) for k in got if st[k] != got[k]}
    print(path, 'step', step + 1, 'retrace', r, 'OK' if not bad else bad)


if __name__ == '__main__':
    # snapshot -> step (from the traced step starts: p6 25596 + cumulative N, p7 29604 + ...)
    for path, part, step in (('snaps/f25700.snap', 6, 0), ('snaps/f26300.snap', 6, 4), ('snaps/f27000.snap', 6, 6),
                             ('snaps/f27900.snap', 6, 9), ('snaps/f28300.snap', 6, 10), ('snaps/f28700.snap', 6, 12),
                             ('snaps/f29100.snap', 6, 14), ('snaps/f29900.snap', 7, 1), ('snaps/f30300.snap', 7, 1),
                             ('snaps/f30700.snap', 7, 4), ('snaps/f31100.snap', 7, 5)):
        check(path, part, step)
