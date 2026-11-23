"""Row starts in full.ticks: frame, irqs at the first record that shows each new (order,row)."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
from sheet import read_ticks
T = read_ticks(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'full.ticks'))
prev = None
out = []
for k in sorted(T):
    t = T[k]
    p = (t.order, t.row)
    if p != prev:
        out.append((k, t.irqs, p))
        prev = p
for i in range(len(out) - 1):
    k, irq, p = out[i]
    print(k, irq, p, out[i + 1][0] - k, out[i + 1][1] - irq)
