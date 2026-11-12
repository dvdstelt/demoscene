"""p7: how often does the light map (base + 5 spots, byte adds with carries) exceed 255 inside the region the
env lookup reaches (rows/cols 64 + 10..117 +- 9 bump offset)?"""
import numpy as np
from check_p7_lightmap import BASE, SPOT, light_offsets
base = BASE.reshape(256, 256).astype(int)
worst = (0, None)
count = 0
for t in range(0, 2100 + 30):
    acc = base.copy()
    for di in light_offsets(t):
        r, c = di >> 8, di & 0xff
        rr = (np.arange(128) + r) % 256
        cc = (np.arange(128) + c)
        assert cc.max() < 256
        acc[np.ix_(rr, cc)] += SPOT
    reg = acc[64 + 1:64 + 127, 64 + 1:64 + 127]
    m = reg.max()
    if m > 255:
        count += 1
    if m > worst[0]:
        worst = (m, t)
print('retraces with a light-map value > 255 in the reachable region:', count, 'of 2130; max', worst)
