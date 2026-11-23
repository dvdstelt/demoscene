"""Records where the part tick counter advanced by less than the number of timer interrupts (callbacks that
returned at once because [0x42278c] was set, or that hit the reentrancy guard), and frame-clock steps != 1."""
import os
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
sys.path.insert(0, RE)
from sheet import read_ticks
T = read_ticks(os.path.join(RE, 'full.ticks'))
ks = sorted(T)
lost = 0
for a, b in zip(ks, ks[1:]):
    ta, tb = T[a], T[b]
    di = tb.irqs - ta.irqs
    dp = tb.part_ticks - ta.part_ticks
    df = tb.ticks - ta.ticks
    if tb.script != ta.script:
        continue
    if ta.order >= 0 and (dp != di or (df != 1 and b > 38)):
        lost += di - dp
        print(b, 'irqs +%d part +%d clock +%g' % (di, dp, df), 'fn %x' % tb.frame_fn, 'eip %x' % tb.eip)
print('lost', lost)
