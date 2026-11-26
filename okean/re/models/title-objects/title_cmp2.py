import sys, time
from title import *
seed = 0x303b
seeds = [seed]
for i in range(620):
    nz, seed = noise(seed)
    seeds.append(seed)
import pickle
pickle.dump(seeds, open('seeds_title.pkl', 'wb'))
for k in (60, 100, 200, 300):
    cidx, pal = cap_frame(k)
    d = k - 5
    for c in range(k - 8, k - 1):
        t = time.time()
        idx, _, out = frame(c, seeds[d])
        print('frame', k, 'c', c, (idx == cidx).mean(), len(out), '%.1fs' % (time.time() - t))
