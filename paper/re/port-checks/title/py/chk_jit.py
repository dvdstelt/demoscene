import sys
from tmodel import *
TAB = [(SEG[0x2790+2*i], SEG[0x2791+2*i]) for i in range(15)]
def visible(c): return sum(a for s, a in TAB if s <= c)
S = 'psychic linksixty four k'
O = osc(140249)
changes = [t for t in range(380, 1040) if counter(t) != counter(t-1)][:12]
for T in changes:
    row = []
    for t in (T-1, T, T+1, T+2):
        idx, _ = cap(t)
        found = None
        for n in (t - 106, t - 107, t - 105, t - 108):
            bg = background(adv(O, n), 7)
            for c in (counter(T-1), counter(T)):
                img = letters(bg, S, jitter(140465 + c*192), visible(c))
                if np.array_equal(img, idx): found = (t - 106 - n, 'new' if c == counter(T) else 'old'); break
            if found: break
        row.append((t, ticks[t][4], found))
    print(T, row)
