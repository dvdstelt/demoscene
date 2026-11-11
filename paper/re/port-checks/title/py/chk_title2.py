import sys
from tmodel import *
TAB = [(SEG[0x2790+2*i], SEG[0x2791+2*i]) for i in range(15)]
def visible(c): return sum(a for s, a in TAB if s <= c)
S = 'psychic linksixty four k'
O = osc(140249)
for t in map(int, sys.argv[1:]):
    idx, pal = cap(t)
    best = []
    for dn in (0, -1, -2):
        n = t - 106 + 1 + dn
        bg = background(adv(O, n), 7)
        for dc in (0, -1):
            c = counter(t + dc)
            img = letters(bg, S, jitter(140465 + c*192), visible(c))
            best.append((int((img != idx).sum()), dn, dc))
    best.sort()
    print(t, counter(t), ticks[t][4], best[:3])
