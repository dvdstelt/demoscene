"""The noise generator of 0x53401 from the part's seed: does it cycle within the 1174 x 16000 values the part uses,
and are neighbouring pixels independent (correlation of the 5-bit indices)."""
import numpy as np
M = 0xffffffff
x = 0x3513c3
N = 1174 * 16000
out = np.empty(N, np.uint32)
for i in range(N):
    x = (x + 0x9d) & M
    x = ((x << 11) | (x >> 21)) & M
    x ^= 0x9d
    out[i] = x
u, first = np.unique(out, return_index=True)
print('values', N, 'distinct', len(u))
if len(u) < N:
    srt = np.sort(first)
    dup = np.setdiff1d(np.arange(N), first)
    print('first repeated value at step', dup[0], 'value', hex(out[dup[0]]), 'first seen at', np.nonzero(out == out[dup[0]])[0][:3])
px = (out[:16000 * 50] & 0x1f1f1f1f).view(np.uint8).astype(float)
for lag in (1, 2, 4, 320):
    print('corr lag', lag, np.corrcoef(px[:-lag], px[lag:])[0, 1])
