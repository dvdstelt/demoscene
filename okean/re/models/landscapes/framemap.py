"""Map captured frames to drawn frames (by the noise, which advances 16000 generator steps per drawn frame) and the
callback count each drawn frame was drawn with. Cached in framemap_<part>.npy."""
import os
import numpy as np
from common import *


def noise_frames(seed, nj):
    x = seed
    out = []
    for j in range(nj):
        o = np.empty(16000, np.uint32)
        for i in range(16000):
            o[i] = x & 0x0f0f0f0f
            x = (x + 0x9d) & 0xffffffff; x = ((x >> 11) | (x << 21)) & 0xffffffff; x ^= 0x9d
        out.append(o.view(np.uint8))
    return out


def framemap(tag, seed, k0, k1, irq0, cap=None):
    """returns list of (j, first capture frame k showing j, callback count n_j)"""
    fn = os.path.join(HERE, 'framemap_%s.npy' % tag)
    if os.path.exists(fn):
        return [tuple(r) for r in np.load(fn).tolist()]
    cap = cap or Capture()
    T = read_ticks_full()
    nf = noise_frames(seed, k1 - k0 + 2)
    shown = {}
    last = 0
    for k in range(k0, k1 + 1):
        c = cap.idx(k).reshape(-1)
        best = max(((np.count_nonzero(nf[j] == c), j) for j in range(max(0, last - 1), min(len(nf), last + 3))))
        if best[0] < 12000:          # not a drawn frame (cleared screen before the first copy)
            continue
        last = best[1]
        shown.setdefault(last, k)
    res = []
    for j in sorted(shown):
        n = 0 if j == 0 else T[shown[j - 1]]['irqs'] - irq0
        res.append((j, shown[j], n))
    np.save(fn, np.array(res))
    return res
