import numpy as np
FR = 64768
d = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
def fr(t): return d[t*FR:t*FR+64000].reshape(200,320)
def pal(t): return d[t*FR+64000:t*FR+64768].reshape(256,3)
# palette changes
prev = None
for t in range(5470, 6030):
    p = np.array(pal(t))
    if prev is None or (p != prev).any():
        ch = np.nonzero((p != prev).any(1))[0] if prev is not None else []
        print('pal change at', t, 'entries', (ch.min(), ch.max(), len(ch)) if len(ch) else '')
    prev = p
p = np.array(pal(5700))
print('pal 0..79 at 5700:')
for i in range(0, 80):
    print(i, p[i], end='; ')
print()
# frame changes
prev = None
for t in range(5600, 6030):
    f = np.array(fr(t))
    if prev is not None:
        dd = f != prev
        n = dd.sum()
        if n:
            ys, xs = np.nonzero(dd)
            vals = np.unique(f[dd])
            print(t, n, 'bbox', xs.min(), ys.min(), xs.max(), ys.max(), 'newvals', vals[:20], len(vals))
    prev = f
