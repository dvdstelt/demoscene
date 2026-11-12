"""p4's state as a function of the IRQ count k (retraces since the timer was installed), as the remake would
compute it, checked against every line of models/p4/out/frames.log (state read by the hook).

Step changes (main loop 1caa): the loop compares the retrace counter [1969] after each frame, so a change takes
effect after IRQ T (or T+1 when an IRQ falls between the comparison and the write: 'late' below).
usage (from dope/re): python3 models/p4/timeline.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# IRQ after which each script step starts, and what it sets
STEPS = [  # (T, [1576] fire phase step, label dir [196f], fade step [1973], lower bound [1a59], A set)
    (0, 35, 0, 1, 0, 0),
    (1400, 31, 1, None, None, None),
    (2886, 29, -1, None, None, None),
    (4166, 27, None, -2, 0x600, None),
    (5216, 25, 1, None, None, None),
    (5916, 23, -1, 5, 0, None),
    (6406, 23, None, -4, None, 0x1000),
]


def state(k, late=()):
    """phase [1578], pos [157a], A [1971], d [196d], P [2113] after IRQ k"""
    phase = pos = 0
    A, Astep, lower = 0, 1, 0
    d, ddir = -1, 0
    fstep = 35
    si = 0
    for j in range(1, k + 1):
        while si + 1 < len(STEPS) and j > STEPS[si + 1][0] + (1 if STEPS[si + 1][0] in late else 0):
            si += 1
            T, fs, dd, ast, lo, aset = STEPS[si]
            fstep = fs
            if dd is not None:
                ddir = dd
            if ast is not None:
                Astep = ast
            if lo is not None:
                lower = lo
            if aset is not None:
                A = aset
        # 1a20
        d += ddir
        if d < 0 or d >= 75:
            ddir = 0
        A += Astep
        if A < lower or A >= 0x1000:
            Astep = 0
        if j & 1:
            pos -= 0x10000
            phase = (phase + fstep) & 0xffff
    return phase, pos, A, d, (2 * k) & 0xffff


def main():
    rows = [l.split() for l in open(os.path.join(HERE, 'out', 'frames.log'))]
    late = set()
    bad = 0
    cache = {}
    for r in rows:
        k = int(r[2])
        got = (int(r[7]), int(r[8]), int(r[4]), int(r[6]), int(r[10]))
        exp = state(k, late)
        if got != exp:
            # try the step change one IRQ late
            for T in [s[0] for s in STEPS[1:]]:
                if T not in late and state(k, late | {T}) == got:
                    late.add(T)
                    print('step after IRQ %d took effect one IRQ late (frame %s)' % (T, r[0]))
                    break
            else:
                bad += 1
                if bad < 5:
                    print('differs at frame %s irq %d: log %s model %s' % (r[0], k, got, exp))
    print('log lines %d, differing %d' % (len(rows), bad))


if __name__ == '__main__':
    main()
