"""Statistics of the end part's noise (0x4d393..0x4d3c2): state x (seed [0x4d36c] = 0x5c17 in the file), per dword
of the buffer: write x & 0x1f1f1f1f, then x = rol(x + 0x9d, 11) ^ 0x9d. 16,000 steps per drawn frame.
Prints the index histogram over the part's draws and the cycle length of the state from the seed (up to a limit).
"""
import numpy as np
SEED = 0x5c17
DRAWS = 1002


def step_vec(n, x):
    out = np.empty(n, np.uint32)
    for i in range(n):
        out[i] = x
        x = (x + 0x9d) & 0xffffffff
        x = ((x << 11) | (x >> 21)) & 0xffffffff
        x ^= 0x9d
    return out, x


xs, x = step_vec(16000 * 50, SEED)
pix = (xs & 0x1f1f1f1f).view(np.uint8)
h = np.bincount(pix, minlength=32)
print('indices 0..31 over 50 frames: min %d max %d mean %.1f (expected %.1f)' % (h.min(), h.max(), h.mean(), len(pix) / 32))
print('relative deviation per index:', np.round(h / h.mean() - 1, 3))
# per-byte-lane histograms (the 4 pixels of a dword come from the 4 bytes of the state)
for lane in range(4):
    hl = np.bincount(pix[lane::4], minlength=32)
    print('lane', lane, 'min/max ratio %.3f' % (hl.min() / hl.max()))
# neighbour correlation
p = pix.reshape(-1, 320).astype(float)
print('horizontal neighbour correlation %.4f, vertical %.4f' % (
    np.corrcoef(p[:, :-1].ravel(), p[:, 1:].ravel())[0, 1], np.corrcoef(p[:-1].ravel(), p[1:].ravel())[0, 1]))
# frame-to-frame: is a frame equal to an earlier one? (cycle search)
x = SEED
lim = 16000 * DRAWS + 1
for i in range(1, lim):
    x = (x + 0x9d) & 0xffffffff
    x = ((x << 11) | (x >> 21)) & 0xffffffff
    x ^= 0x9d
    if x == SEED:
        print('state returns to the seed after', i, 'steps')
        break
else:
    print('no return to the seed within', lim - 1, 'steps (the whole part): no repeated frames')
