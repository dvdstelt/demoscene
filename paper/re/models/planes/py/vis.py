import numpy as np
FR = 64768
data = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
prev = None
cnts = []
for t in range(6150, 7741):
    f = data[t * FR:t * FR + 64000]
    n = int(((f >= 20) & (f < 64)).sum())
    cnts.append((t, n))
first = next(t for t, n in cnts if n > 0)
print('first plane pixels at tick', first)
print([(t, n) for t, n in cnts if t % 40 == 0][:45])
