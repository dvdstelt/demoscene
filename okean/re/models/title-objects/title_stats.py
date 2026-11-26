"""index statistics of the title frames (model): max stamp count, wrap-around, odd palette entries 111 and 254"""
import pickle
from title import *
s = snap('2c26f')
rd = snap_mask_reader(s)
seeds = seed_list(620)
for c in range(0, 616, 25):
    nz, _ = noise(seeds[c])
    idx, _, _ = frame(c, seeds[c], rd)
    cnt = (idx.astype(int) - nz.reshape(200, 320)) % 256
    print('c', c, 'max stamps(mod256)', cnt.max(), 'px>=48', int((idx >= 48).sum()), 'px==111', int((idx == 111).sum()), 'px==254', int((idx == 254).sum()),
          'px>=104', int((idx >= 104).sum()))
