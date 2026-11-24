import sys
from title import *
seed = 0x303b
seeds = [seed]
for i in range(8):
    nz, seed = noise(seed)
    seeds.append(seed)
for k in range(5, 9):
    cidx, pal = cap_frame(k)
    for d in range(4):
        nz, _ = noise(seeds[d])
        eq = (nz.reshape(200, 320) == cidx).mean()
        if eq > 0.5:
            print('frame', k, 'draw', d, 'noise match', eq)
            for c in range(-2, 8):
                idx, _, out = frame(c, seeds[d])
                print('   c', c, (idx == cidx).mean(), len(out))
