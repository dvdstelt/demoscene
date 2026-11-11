from tmodel import *
O = osc(140249)
for t in (103, 104, 105, 106, 107, 108, 10967, 10968, 10969, 10970, 10971, 11376, 11377, 11378, 12233, 12234):
    idx, pal = cap(t)
    rgb = pal[idx]
    extra = ''
    if 104 <= t <= 108:
        for n in range(0, 4):
            bg = background(adv(O, n), 7)
            if np.array_equal(bg, idx): extra += ' bg n=%d' % n
    print(t, ticks[t][2:], 'idx mean %.1f max %d' % (idx.mean(), idx.max()), 'rgb max', rgb.max(), extra)
