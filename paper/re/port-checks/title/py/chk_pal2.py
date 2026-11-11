from chk_pal import *
import collections
def offsets(t0, t1, base, sh=11):
    cnt = collections.Counter(); n = 0
    for t in range(t0, t1):
        _, pal = cap(t)
        n += 1
        for off in range(-4, 5):
            a = base + t + off
            p, _ = model.palette(a, sh)
            if np.array_equal(model.fade(p, LV[t]), pal.astype(np.int32)): cnt[off] += 1
    return n, sorted(cnt.items())
print('title angle = t - 106 + off:', offsets(110, 1040, -106))
print('end angle = 2910 + t - 10970 + off:', offsets(10975, 11340, 2910 - 10970))
