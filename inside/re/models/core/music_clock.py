"""Check the music clock model against full.ticks and full.gus.

Model (see notes-core.md "Clocks"):
  M = [0x4248c4] = timer callbacks since the music start (callback 1 is the first interrupt after init installs
      0x40070c); DSMI time = 35 * M.  In the capture callback M is timer interrupt M + 90, except that one callback
      at record 3045 returned at once ([0x42278c] set by the credits), so from there M = interrupt - 91.
  Player tick k (k = 1, 2, ...) runs in callback M_k = floor(sum of the first k periods / 35) + 1 (the poll ticks
      while DSMI time - last > period, strictly).  Ticks 1..6 at period 10000 // 50 = 200 (DSMI's default 50 Hz),
      row 0 at tick 6; after that every row takes 768 units (speed * period).
  So song row R (the R-th row played, from 0) starts in callback c_R = floor((1200 + 768 R) / 35) + 1.
"""
import os
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
sys.path.insert(0, RE)
from sheet import read_ticks

T = read_ticks(os.path.join(RE, 'full.ticks'))
ks = sorted(T)
LOST_AT = 3045                      # record whose interval contains the callback that returned at once


def M_of_record(k):
    return T[k].irqs - 90 - (1 if k >= LOST_AT else 0)


def c_row(R, order36=True):
    if R <= 2304 or not order36:
        return (1200 + 768 * R) // 35 + 1
    return (1200 + 768 * 2304 + 2304 * (R - 2304)) // 35 + 1     # order 36: rows of 2,304 units


# observed row changes: (record, (order,row))
changes = []
prev = None
for k in ks:
    t = T[k]
    if t.order < 0:
        continue
    p = (t.order, t.row)
    if p != prev:
        changes.append((k, p))
        prev = p
bad = 0
for R, (k, p) in enumerate(changes):
    if R == 0:
        continue                    # (0,0) is set by ampPlayModule before any tick
    c = c_row(R, '--no36' not in sys.argv)
    lo, hi = M_of_record(k - 1), M_of_record(k)
    if not (lo < c <= hi):
        bad += 1
        if bad < 20:
            print('row %d %s: predicted callback %d, record %d covers %d..%d' % (R, p, c, k, lo + 1, hi))
print('rows checked', len(changes) - 1, 'mismatches', bad)

# GUS writes: interrupts that carry writes, against row starts (interrupt = M + 90 (+1 after the lost one))
gus_irqs = []
last = None
for line in open(os.path.join(RE, 'full.gus')):
    i = int(line.split()[0])
    if i != last:
        gus_irqs.append(i)
        last = i
rows_irq = set()
lost_irq = M_of_record(LOST_AT) + 91      # about where the lost callback is
for R in range(len(changes)):
    c = c_row(R) if R else c_row(0)
    irq = c + 90 if c + 90 < T[LOST_AT - 1].irqs else c + 91
    rows_irq.add(irq)
row0 = c_row(0) + 90
other = [i for i in gus_irqs if i >= row0 and i not in rows_irq]
missing = [i for i in sorted(rows_irq) if i not in set(gus_irqs)]
print('row 0 at callback', c_row(0), 'interrupt', row0)
print('GUS write interrupts after row 0 that are not row starts:', len(other), other[:20])
print('row starts without GUS writes:', len(missing), missing[:20])
print('last row', changes[-1], 'R =', len(changes) - 1)
