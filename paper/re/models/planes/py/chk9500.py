import numpy as np
from snap import M
m = M(9500)
FR = 64768
data = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
vga = np.frombuffer(m.b(0xa0000, 64000), np.uint8)
for t in (9498, 9499, 9500):
    print(t, (data[t * FR:t * FR + 64000] == vga).mean())
print('pal eq', bytes(data[9499 * FR + 64000:9500 * FR]) == m.s['pal'])
