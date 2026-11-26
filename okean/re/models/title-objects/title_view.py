"""side by side: capture frame, the model, and the model's outer and inner beams separately (stamp counts x8)"""
import sys, pickle
from title import *
from PIL import Image
seeds = seed_list(620)
s = snap('2c26f'); rd = snap_mask_reader(s)
rows = []
for k in map(int, sys.argv[1:]):
    c = k - 5
    cidx, cpal = cap_frame(k)
    x, y = points(c)
    X1 = tdiv(x << 7, 1024) + 160; Y1 = tdiv(y << 7, 1024) + 100
    X2 = tdiv(x << 7, 16) + 160; Y2 = tdiv(y << 7, 16) + 100
    outer = np.zeros((208, 328), np.int64); inner = np.zeros((208, 328), np.int64)
    for i in range(256):
        if rd(int(Y1[i]) * 320 + int(X1[i])) == 0:
            beam_outer(outer, int(X1[i]), int(Y1[i]), int(X2[i]), int(Y2[i]))
        beam_inner(inner, int(X1[i]), int(Y1[i]), lambda o: MASK_FILE[o])
    pal = cpal.astype(np.int64) * 255 // 63
    g = lambda a: np.repeat(np.clip(a[:200, :320] * 8, 0, 255)[:, :, None], 3, 2)
    rows.append(np.concatenate([pal[cidx], g(outer), g(inner)], 1))
Image.fromarray(np.concatenate(rows, 0).astype(np.uint8)).save('title_view.png')
