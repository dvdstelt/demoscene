import sys
from tmodel import *
TAB = [(SEG[0x2790+2*i], SEG[0x2791+2*i]) for i in range(15)]
def visible(c): return sum(a for s, a in TAB if s <= c)
S = 'psychic linksixty four k'
O = osc(140249)
for t in map(int, sys.argv[1:]):
    c = counter(t)
    idx, pal = cap(t)
    res = []
    for dn in (0, -1):
        n = t - 106 + 1 + dn
        bg = background(adv(O, n), 7)
        for name, pos in (('A r=c', 140465 + c*192), ('B r=c-1', 140465 + (c-1)*192 if c > 0 else 140057)):
            img = letters(bg, S, jitter(pos), visible(c))
            res.append((dn, name, int((img != idx).sum())))
    print(t, 'counter', c, 'vis', visible(c), res)
