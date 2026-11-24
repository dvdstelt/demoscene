import sys, copy, pickle
from objects import *
from PIL import Image
s = snap('2e779')
st = init_from_snapshot(s)
seeds = seed_list(1700, mi32(s, 0x2c9b4) & M32)
tk = ticks()
pos = lambda f: (int(tk[f][2]), int(tk[f][3]))
K = int(sys.argv[1])
for k in range(624, K + 1):
    if k > 624:
        callback(st, *pos(k - 1))
sd = copy.deepcopy(st)
bg, _ = background(sd['bg1'], sd['bg2'], seeds[K - 624])
img, _ = draw(sd, pos(K - 1)[0], seeds[K - 624])
cidx, cpal = cap_frame(K)
cap_st = (cidx.astype(int) - bg) % 256
mod_st = (img.astype(int) - bg) % 256
print('stamp sums model', mod_st.sum(), 'capture', cap_st.sum())
y, x = 100, 160
print(mod_st[90:106, 150:170])
print(cap_st[90:106, 150:170])
im = np.concatenate([mod_st, cap_st], 1)
Image.fromarray(np.clip(im * 8, 0, 255).astype(np.uint8)).resize((1280, 400), Image.NEAREST).save('dbg4_%d.png' % K)
