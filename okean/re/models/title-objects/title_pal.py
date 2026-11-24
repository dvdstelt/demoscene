from title import *
P0 = np.frombuffer(frd(0x2b65c, 768), np.uint8).astype(np.int64)
tk = ticks()
prev = None
for k in range(3, 625):
    idx, pal = cap_frame(k)
    pal = pal.reshape(-1).astype(np.int64)
    if (pal == P0).all():
        lab = 'P0'
    else:
        lab = None
        for f in range(0, 0x101, 8):
            if (np.clip((P0 * f) >> 8, 0, 63) == pal).all():
                lab = 'f=%d' % f
                break
        if lab is None:
            lab = 'other (max %d)' % pal.max()
    if lab != prev:
        print(k, tk[k][2:4], 'irqs', tk[k][9], lab)
    prev = lab
