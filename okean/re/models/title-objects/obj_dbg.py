import sys, copy
from objects import *
from PIL import Image
s = snap('2e779')
st = init_from_snapshot(s)
seed = mi32(s, 0x2c9b4) & M32
tk = ticks()
pos = lambda f: (int(tk[f][2]), int(tk[f][3]))
K = int(sys.argv[1])
for k in range(624, K + 1):
    if k > 624:
        callback(st, *pos(k - 1))
    if k == K:
        sd = copy.deepcopy(st)
        print(sd)
        P = project(sd)
        print(P[:8])
        img, _ = draw(sd, pos(k - 1)[0], seed)
        cidx, cpal = cap_frame(k)
        pal = cpal.astype(np.uint16) * 255 // 63
        Image.fromarray(np.concatenate([pal[img], pal[cidx]], 1).astype(np.uint8)).resize((1280, 400), Image.NEAREST).save('dbg_%d.png' % k)
        d = (img.astype(int) - cidx.astype(int))
        print('diff hist', np.unique(d, return_counts=True))
    _, seed = background(st['bg1'], st['bg2'], seed)
