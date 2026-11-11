import sys
from tmodel import *
S = '   t h e          e n d\0'
O = osc(285524)
print('counters', [(t, counter(t), ticks[t][2], ticks[t][4]) for t in range(10966, 10976)])
for t in [int(a) for a in sys.argv[1:]]:
    idx, _ = cap(t)
    found = []
    for n in range(t - 10972, t - 10966):
        bg = background(adv(O, n), 5)
        for r in [-1] + list(range(0, 60)):
            pos = 285332 if r < 0 else 285740 + r*192
            img = letters(bg, S, jitter(pos), 24)
            d = int((img != idx).sum())
            if d == 0: found.append((t - n, r))
    print(t, counter(t), ticks[t][4], 'n = t - ', found)
