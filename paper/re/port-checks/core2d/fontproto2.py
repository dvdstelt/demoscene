import numpy as np
from PIL import Image
seg = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()[0xd00:]
p = 0x18ed7
glyphs = []
for g in range(29):
    w = seg[p]; p += 1
    bm = np.zeros((32, 60), np.uint8)
    for row in range(32):
        x = 0
        while True:
            b = seg[p]; p += 1
            if b == 0: break
            if b == 0xff: skip, run = seg[p], seg[p+1]; p += 2
            else: skip, run = b & 15, b >> 4
            x += skip
            for _ in range(run): bm[row, x] = 1; x += 1
    glyphs.append((w, bm))
print('font bytes', p - 0x18ed7)
K = 12
def wmat(n, K, kind, sigma=0):
    W = np.zeros((n*K, n))
    for o in range(n*K):
        u = (o + 0.5)/K - 0.5
        i0 = int(np.floor(u)); f = u - i0
        if kind == 'bilinear': taps = {i0: 1-f, i0+1: f}
        else:
            w = [f*(-0.5+f*(1-0.5*f)), 1+f*f*(-2.5+1.5*f), f*(0.5+f*(2-1.5*f)), f*f*(-0.5+0.5*f)]
            taps = {i0-1+k: w[k] for k in range(4)}
        for i, wt in taps.items(): W[o, min(max(i, 0), n-1)] += wt
    if sigma:
        G = np.zeros((n, n))
        for i in range(n):
            for j in range(n): G[i, j] = np.exp(-0.5*(i-j)**2/sigma**2)
        G /= G.sum(axis=1, keepdims=True)
        W = W @ G
    return W
def up(bm, method):
    pad = np.pad(bm.astype(np.float32), 3)
    h, w = pad.shape
    kind = 'bilinear' if method == 'bilinear' else 'cubic'
    sg = float(method[5:]) if method.startswith('gauss') else 0
    z = wmat(h, K, kind, sg) @ pad @ wmat(w, K, kind, sg).T
    return (z >= 0.5).astype(np.uint8) * 255
txt = 'agskwoy'
rows = []
for method in ('nearest', 'gauss0.6', 'gauss0.8', 'gauss1.0'):
    tiles = []
    for ch in txt:
        gi = '{|}'.index(ch) + 26 if ch in '{|}' else (ord(ch) - 97 if ch.isalpha() else 27)
        w, bm = glyphs[gi]
        b = bm[:, :w + 1]
        if method == 'nearest': t = np.kron(np.pad(b, 3), np.ones((K, K), np.uint8)) * 255
        else: t = up(b, method)
        tiles.append(t)
    rows.append(np.concatenate(tiles, axis=1))
W = max(r.shape[1] for r in rows)
img = np.concatenate([np.pad(r, ((0, 4), (0, W - r.shape[1]))) for r in rows], axis=0)
Image.fromarray(img).save('font_methods2.png')
print(img.shape)
