"""What water/water.int (256x256 blend table, fish: index = int[texel * 256 + background]) and water/water.shd
(64 x 256 shade table, loaded into the obuke's material but not read by its rasterizer 0x412db0) contain:
fits of the table's colours against blends of the palette's colours (water/water.pal)."""
import sys
import numpy as np
sys.path.insert(0, '../..')
import datfile

d = datfile.Dat('../../../original/INSIDE.DAT')
P = np.frombuffer(d.get('water/water.pal'), np.uint8).reshape(256, 3).astype(float)
I = np.frombuffer(d.get('water/water.int'), np.uint8).reshape(256, 256)
S = np.frombuffer(d.get('water/water.shd'), np.uint8).reshape(64, 256)


def nearest(c):
    """index of the palette entry closest to colour c (6-bit RGB, Euclidean)"""
    return int(np.argmin(((P - c) ** 2).sum(1)))


# --- water.int: rows = first index (the env texel), columns = second index (the background pixel)
C = P[I]                                   # (256, 256, 3) the colours the table gives
A = P[:, None, :].repeat(256, 1)           # colour of the row index
B = P[None, :, :].repeat(256, 0)           # colour of the column index
X = np.stack([A.reshape(-1, 3), B.reshape(-1, 3), np.ones((65536, 3))], 1)   # per channel
best = None
for name, model in (('a*A + b*B + c', None),):
    M = np.c_[A.reshape(-1), B.reshape(-1), np.ones(65536 * 3)]
    coef, *_ = np.linalg.lstsq(M, C.reshape(-1), rcond=None)
    err = np.abs(M @ coef - C.reshape(-1))
    print('water.int: colour(int[a][b]) ~ %.3f pal[a] + %.3f pal[b] + %.2f  (mean err %.2f, 95%% %.2f of 63)' % (
        coef[0], coef[1], coef[2], err.mean(), np.percentile(err, 95)))
for name, f in (('(A+B)/2', lambda: (A + B) / 2), ('min(A+B,63)', lambda: np.minimum(A + B, 63)),
                ('A/2+B', lambda: np.minimum(A / 2 + B, 63)), ('max', lambda: np.maximum(A, B)),
                ('B + (A-B)/4', lambda: B + (A - B) / 4), ('B + (A-B)*3/8', lambda: B + (A - B) * 3 / 8),
                ('screen', lambda: 63 - (63 - A) * (63 - B) / 63)):
    T = f()
    err = np.abs(T - C)
    # how often the table entry is the nearest palette colour to the model colour
    hit = np.mean([I[a, b] == nearest(T[a, b]) for a in range(0, 256, 7) for b in range(0, 256, 7)])
    print('  %-14s mean |err| %.2f   table == nearest(model) %.0f%%' % (name, err.mean(), 100 * hit))
print('diagonal int[a][a] == a: %.0f%%' % (100 * np.mean([I[a, a] == a for a in range(256)])))
print('int[0][b] == b: %.0f%%, int[a][0] == a: %.0f%%' % (100 * np.mean(I[0] == np.arange(256)),
                                                          100 * np.mean(I[:, 0] == np.arange(256))))
print('symmetric: %.0f%%' % (100 * np.mean(I == I.T)))

# --- water.shd: rows = levels 0..63, columns = colour index
lum = P.mean(1)
for lv in (0, 16, 31, 32, 48, 63):
    ratio = np.median([P[S[lv, c]].mean() / max(lum[c], 1) for c in range(256) if lum[c] > 8])
    print('water.shd level %2d: colour ~ %.2f x pal[c] (median brightness ratio), shd[%d][c]==c %.0f%%' % (
        lv, ratio, lv, 100 * np.mean(S[lv] == np.arange(256))))
