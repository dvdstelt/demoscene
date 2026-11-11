import numpy as np
from PIL import Image, ImageDraw
exec(open('fontproto.py').read().split("K = 12")[0])   # glyphs
def trace(bm):
    h, w = bm.shape
    inside = lambda x, y: 0 <= x < w and 0 <= y < h and bm[y, x]
    edges = {}
    for y in range(h):
        for x in range(w):
            if not bm[y, x]: continue
            if not inside(x, y-1): edges.setdefault((x, y), []).append((x+1, y))
            if not inside(x+1, y): edges.setdefault((x+1, y), []).append((x+1, y+1))
            if not inside(x, y+1): edges.setdefault((x+1, y+1), []).append((x, y+1))
            if not inside(x-1, y): edges.setdefault((x, y+1), []).append((x, y))
    loops = []
    while edges:
        start = next(iter(edges)); a = start
        b = edges[a].pop(0)
        if not edges[a]: del edges[a]
        pts = [a]; d = (b[0]-a[0], b[1]-a[1])
        while b != start:
            pts.append(b)
            outs = edges[b]
            if len(outs) > 1:   # saddle: prefer the right turn (hug the inside)
                def turn(o):
                    e = (o[0]-b[0], o[1]-b[1]); return d[0]*e[1] - d[1]*e[0]
                outs.sort(key=lambda o: -turn(o))
            c = outs.pop(0)
            if not outs: del edges[b]
            d = (c[0]-b[0], c[1]-b[1]); a, b = b, c
        loops.append(np.array(pts, float))
    return loops
def smooth(P, it=300, tube=0.5, final=3, longrun=3):
    n = len(P)
    d = np.roll(P, -1, 0) - P                     # direction of the unit edge leaving each point
    # run length through each vertex: pinned if incoming and outgoing runs are both long and directions differ
    dirs = [tuple(v) for v in d]
    runlen_out = np.zeros(n, int); runlen_in = np.zeros(n, int)
    for i in range(n):
        k = 0
        while k < n and dirs[(i+k) % n] == dirs[i]: k += 1
        runlen_out[i] = k
        k = 0
        while k < n and dirs[(i-1-k) % n] == dirs[(i-1) % n]: k += 1
        runlen_in[i] = k
    pinned = np.array([dirs[i] != dirs[i-1] and runlen_out[i] >= longrun and runlen_in[i] >= longrun for i in range(n)])
    Q = P.copy()
    for _ in range(it):
        avg = 0.5 * (np.roll(Q, 1, 0) + np.roll(Q, -1, 0))
        Q = np.where(pinned[:, None], P, np.clip(avg, P - tube, P + tube))
    for _ in range(final):
        avg = 0.25 * np.roll(Q, 1, 0) + 0.5 * Q + 0.25 * np.roll(Q, -1, 0)
        Q = np.where(pinned[:, None], P, avg)
    return Q
K = 12
txt = 'agskwoy9e'
rows = []
for variant in ('trace0.5', 'trace0.5b'):
    tiles = []
    for ch in txt:
        gi = '{|}'.index(ch) + 26 if ch in '{|}' else (ord(ch) - 97 if ch.isalpha() else 27)
        w, bm = glyphs[gi]
        b = np.pad(bm[:, :w], 3)
        loops = trace(b)
        im = Image.new('L', (b.shape[1]*K, b.shape[0]*K), 0)
        dr = ImageDraw.Draw(im)
        # outer loops are clockwise in y-down (positive area with this formula); fill outer then holes
        def area(L): return 0.5*np.sum(L[:,0]*np.roll(L[:,1],-1) - np.roll(L[:,0],-1)*L[:,1])
        ls = [smooth(L, final=0 if variant == 'trace0.5' else 4) for L in loops]
        for L in sorted(ls, key=lambda L: -area(L)):
            dr.polygon([tuple(p*K) for p in L], fill=255 if area(L) > 0 else 0)
        tiles.append(np.asarray(im))
    rows.append(np.concatenate(tiles, axis=1))
W = max(r.shape[1] for r in rows)
img = np.concatenate([np.pad(r, ((0, 4), (0, W - r.shape[1]))) for r in rows], axis=0)
Image.fromarray(img).save('font_trace.png'); print(img.shape)
