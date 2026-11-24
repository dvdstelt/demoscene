"""Statistics for the notes: the noise's index distribution, how bright the lines get (sum of stamps per pixel),
how often the byte-wise dword adds wrap past 255 (and carry into the right neighbour), stamps per frame."""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sheet_model as sm
from sheet_model import Tables, State, heights, noise, project

T = Tables()
K = np.zeros((8, 8), np.int64)
for r, (a, b) in enumerate(T.stamp):
    K[r, :4] = [(a >> (8 * i)) & 0xff for i in range(4)]
    K[r, 4:] = [(b >> (8 * i)) & 0xff for i in range(4)]
print('stamp kernel\n', K, 'sum', K.sum(), 'row sums', K.sum(1))

stamps = []
orig_line = sm.line


def stamp_positions(x0, y0, x1, y1):
    """the stamp corners line() visits (same arithmetic, no buffer)"""
    out = []
    M = sm.M32
    if not y0 > 0 and y1 < 0: return out
    if not x0 > 0 and x1 < 0: return out
    if not x0 < 0x13f and x1 > 0x13f: return out
    if not y0 < 0xc7 and y1 > 0xc7: return out
    if not (y0 & M) < (y1 & M):
        x0, y0, x1, y1 = x1, y1, x0, y0
    dx = sm.s32(x1 - x0); dy = sm.s32(y1 - y0)
    nx = (dx + 1 if dx >= 0 else -dx); ny = (dy + 1 if dy >= 0 else -dy)
    n = max(nx, ny) >> 1
    if n == 0: return out
    sx = sm.div8(dx, n); sy = sm.div8(dy, n)
    fx = (x0 << 8) & M; fy = (y0 << 8) & M
    for _ in range(((n - 1) & 0xff) + 1):
        py = sm.s32(fy) >> 8; px = sm.s32(fx) >> 8
        if 0 <= py <= 0xbf and 0 <= px <= 0x137:
            out.append((px, py))
        fy = (fy + sy) & M; fx = (fx + sx) & M
    return out


seed = T.seed0
hist = np.zeros(32, np.int64)
wraps, maxs, nst, longest = [], [], [], 0
for c in list(range(0, 1137, 7)) + list(range(1137, 1202)):
    st = State(T, c, max(0, c - 1137))
    buf = np.empty(64000, np.uint8)
    seed = noise(buf, seed)
    if c < 100:
        hist += np.bincount(buf, minlength=32)
    z = heights(T, st)
    pts = project(T, st, z)
    acc = buf.astype(np.int64).reshape(200, 320)
    n = 0
    for e0, e1 in T.edges:
        ps = stamp_positions(*pts[e0], *pts[e1])
        n += len(ps)
        longest = max(longest, len(ps))
        for px, py in ps:
            acc[py:py + 8, px:px + 8] += K
    nst.append(n)
    maxs.append(int(acc.max()))
    wraps.append(int((acc > 255).sum()))
print('noise index histogram (first frames): min %d max %d mean index %.2f' % (hist.min(), hist.max(),
      (hist * np.arange(32)).sum() / hist.sum()))
print('stamps per frame: min %d max %d mean %.0f; most stamps on one edge %d' % (min(nst), max(nst), np.mean(nst), longest))
print('max intensity per frame: min %d max %d mean %.0f' % (min(maxs), max(maxs), np.mean(maxs)))
print('pixels > 255 per frame: frames with any %d of %d, max %d' % (sum(1 for w in wraps if w), len(wraps), max(wraps)))
print('pal1 first index with 63,63,63:', int(np.nonzero((T.pal1.reshape(256, 3) == 63).all(1))[0][0]))
