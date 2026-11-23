"""Player tick times: every interrupt that carries GUS writes after the music start must be a player tick of the
model: ticks 1..6 at period 200 (DSMI's default 50 Hz), row 0 on tick 6, then period 10000 // 52 = 192 with 4 ticks
per row, 12 ticks per row from order 36 on (inferred from the row lengths: 768 and 2304 units). DSMI time = 35 * M,
M = callbacks since the music start; interrupt = M + 90 (+1 after the callback lost in the credits at record 3045).
"""
import os
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
sys.path.insert(0, RE)
from sheet import read_ticks

T = read_ticks(os.path.join(RE, 'full.ticks'))
LOST_IRQ_AFTER = T[3044].irqs            # the lost callback is between records 3044 and 3045
ROW_36 = 2304                            # song row index of 36:00


def irq_of(M):
    i = M + 90
    return i if i <= LOST_IRQ_AFTER else i + 1


ticks = []                               # (callback M, song row or None)
last, t, k = 0, 0, 0
period = 200
row = -1
tick_in_row = 0
speed = 6
while True:
    # find callback where 35*M - last > period
    M = (last + period) // 35 + 1
    last += period
    tick_in_row += 1
    is_row = False
    if tick_in_row >= speed:
        tick_in_row = 0
        row += 1
        is_row = True
        if row == 0:
            period, speed = 192, 4
        if row == ROW_36:
            speed = 12
    ticks.append((M, row if is_row else None))
    if row > 2330:
        break
tick_irqs = {irq_of(M) for M, _ in ticks}
gus = sorted({int(l.split()[0]) for l in open(os.path.join(RE, 'full.gus'))})
start = irq_of(ticks[0][0])
odd = [i for i in gus if i >= start and i not in tick_irqs]
print('first tick at interrupt', start, 'row 0 at interrupt', irq_of(ticks[5][0]))
print('GUS write interrupts after the first tick:', len([i for i in gus if i >= start]), 'not on a model tick:',
      len(odd), odd[:30])
