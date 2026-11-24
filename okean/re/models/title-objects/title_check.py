import sys
from title import *
s = snap('2c26f')
seed = mi32(s, 0x14d1c) & 0xffffffff
print('seed', hex(seed))
# T check against the next part's snapshot
s2 = snap('2e779')
Tm = mi32(s2, 0x24a3c, 0x1401).astype(np.int64)
print('sine table equal:', (Tm == T).all(), np.nonzero(Tm != T)[0][:10])
# first frames: compare noise-only to capture
for k in range(3, 9):
    idx, pal = cap_frame(k)
    print(k, np.unique(idx)[:12], idx[:2, :8].tolist())
