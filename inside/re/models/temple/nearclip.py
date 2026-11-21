"""Frame 11519 (out/t3.pkl): pixels whose first write is a translucent face (nothing opaque behind them). Are they
inside the properly near-clipped projection of a front-facing face of the shell (object 2) or the bowl (object 0)
that crosses the near plane?"""
import numpy as np, collections
from PIL import Image, ImageDraw
import dumpread as D
NEAR = 64.0
d = D.load('out/t3.pkl')
rec = [r for r in d if 'pix' in r][0]; m = D.Blocks(rec)
fn = {f: k for f, kk, k in rec['faces']}
first = {}
for f, off, size, val, old in rec['pix']:
    first.setdefault(off, fn.get(f, 0))
holes = np.array([first.get(o) in (0x41669c, 0x416dc8, 0x415dc8) for o in range(64000)]).reshape(200, 320)
cam = m.d32(0x425e74)
Cw = m.mat(cam + 0x70); cpos = np.array(m.ints(cam + 0x1c, 3), float)
mask = Image.new('L', (320, 200), 0); dr = ImageDraw.Draw(mask)
crossing = 0
for oi in (0, 2):
    o = m.d32(0x425de0 + 4 * oi)
    Mw = m.mat(o + 0x70); pos = np.array(m.ints(o + 0x1c, 3), float)
    f0 = m.d32(o + 0xd8)
    for k in range(m.i32(o + 0xdc)):
        f = f0 + 0x34 * k
        if not m.d32(f + 0x30) & 2: continue
        P = [Cw.T @ (Mw @ np.array(m.ints(m.d32(f + q) + 8, 3), float) + pos - cpos) for q in (0x14, 0x18, 0x1c)]
        if all(p[2] >= NEAR for p in P): continue
        crossing += 1
        # Sutherland-Hodgman against z = NEAR
        out = []
        for i in range(3):
            a, b = P[i], P[(i + 1) % 3]
            if a[2] >= NEAR: out.append(a)
            if (a[2] >= NEAR) != (b[2] >= NEAR):
                t = (NEAR - a[2]) / (b[2] - a[2]); out.append(a + t * (b - a))
        if len(out) >= 3:
            dr.polygon([(160 + 170 * p[0] / p[2], 100 + 170 * 5 / 6 * p[1] / p[2]) for p in out], fill=255)
inside = np.array(mask) > 0
print('frame', rec['frame'], 'front faces of objects 0/2 crossing the near plane:', crossing)
print('translucent-first pixels', holes.sum(), 'of them inside a near-crossing face (properly clipped):', (holes & inside).sum())

# cast the view rays of the hole pixels against every face of objects 0, 1, 2 (any facing)
tris = []
for oi in (0, 1, 2):
    o = m.d32(0x425de0 + 4 * oi)
    Mw = m.mat(o + 0x70); pos = np.array(m.ints(o + 0x1c, 3), float)
    f0 = m.d32(o + 0xd8)
    for k in range(m.i32(o + 0xdc)):
        f = f0 + 0x34 * k
        P = [Cw.T @ (Mw @ np.array(m.ints(m.d32(f + q) + 8, 3), float) + pos - cpos) for q in (0x14, 0x18, 0x1c)]
        tris.append((oi, bool(m.d32(f + 0x30) & 2), P))
def hit(dirv):
    best = None
    for oi, front, (a, b, c) in tris:
        e1, e2 = b - a, c - a
        h = np.cross(dirv, e2); det = e1 @ h
        if abs(det) < 1e-9: continue
        s = -a; u = (s @ h) / det
        if u < 0 or u > 1: continue
        q = np.cross(s, e1); v = (dirv @ q) / det
        if v < 0 or u + v > 1: continue
        t = (e2 @ q) / det
        if t > 1e-6 and (best is None or t < best[0]): best = (t, oi, front)
    return best
ys, xs = np.nonzero(holes)
res = collections.Counter()
for y, x in list(zip(ys, xs))[::7]:
    dv = np.array([(x + 0.5 - 160) / 170, (y + 0.5 - 100) / (170 * 5 / 6), 1.0])
    h = hit(dv)
    res['no hit' if h is None else 'obj %d %s, depth %s' % (h[1], 'front' if h[2] else 'back', '<64' if h[0] < 64 else '>=64')] += 1
print('rays through hole pixels (every 7th):', dict(res))

tris = [t for t in tris if t[0] != 1]
res = collections.Counter(); samples = []
for y, x in list(zip(ys, xs))[::7]:
    dv = np.array([(x + 0.5 - 160) / 170, (y + 0.5 - 100) / (170 * 5 / 6), 1.0])
    h = hit(dv)
    key = 'no hit' if h is None else 'obj %d %s' % (h[1], 'front' if h[2] else 'back')
    res[key] += 1
    if len(samples) < 6: samples.append((x, y, key, None if h is None else round(h[0], 1)))
print('first opaque (object 0 / 2) hit of those rays:', dict(res), samples)
