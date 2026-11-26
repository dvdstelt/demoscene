"""which off-screen start points would get an outer beam (the y-clip quirk of 0x2bc7c), and what the mask test read"""
from title import *
s = snap('2c26f')
rd = snap_mask_reader(s)
for c in range(0, 616):
    x, y = points(c)
    X1 = tdiv(x << 7, 1024) + 160; Y1 = tdiv(y << 7, 1024) + 100
    X2 = tdiv(x << 7, 16) + 160; Y2 = tdiv(y << 7, 16) + 100
    for i in range(256):
        off = int(Y1[i]) * 320 + int(X1[i])
        inside = 0 <= X1[i] < 320 and 0 <= Y1[i] < 200
        acc = np.zeros((208, 328), np.int64)
        nst = beam_outer(acc, int(X1[i]), int(Y1[i]), int(X2[i]), int(Y2[i]))
        if nst and not inside:
            print('c', c, 'beam', i, 'P1', int(X1[i]), int(Y1[i]), 'P2', int(X2[i]), int(Y2[i]), 'stamps', nst, 'read [%x] = %d' % (0x1502c + off, rd(off)))
        if nst and inside and Y2[i] < 0:
            pass
