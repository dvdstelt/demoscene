"""The temple's meshes as the engine holds them (0x410bb0: sro coordinates x 6553/65536, truncated; putki moved and
scaled by 0x4039c8), a summary and a top / side view (out/meshes.png)."""
import os, sys, struct, math
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import datfile
d = datfile.Dat(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'original', 'INSIDE.DAT'))


def sro(name):
    b = d.get(name)
    voff, nv, foff, nf = struct.unpack('<4I', b[4:20])
    V = np.array([struct.unpack('<6h', b[voff + 12 * i: voff + 12 * i + 12]) for i in range(nv)])
    F = np.array([struct.unpack('<4h', b[foff + 8 * i: foff + 8 * i + 8]) for i in range(nf)])
    return b, V, F


def loaded(name):
    """vertices as 0x410bb0 stores them: trunc(c * (6553 * 1.52587890625e-05))"""
    b, V, F = sro(name)
    s = 6553 * (1 / 65536)
    P = np.trunc(V[:, :3] * s).astype(int)
    return P, F[:, 1:], V, F


def putki(sx, sy, sz, pivot=True):
    P, F, V, Fr = loaded('temple/putki.sro')
    if pivot:
        P = P - P[12]
    P = np.trunc(P * np.array([sx, sy, sz], np.float32).astype(float)).astype(int)
    return P, F


if __name__ == '__main__':
    for n in ('mesta21', 'mesta22', 'mesta23', 'putki'):
        b, V, F = sro('temple/%s.sro' % n)
        hdr = struct.unpack('<5I', b[:20])
        P, Fi, _, _ = loaded('temple/%s.sro' % n)
        print(n, 'header', [hex(x) for x in hdr], 'vertices', len(V), 'faces', len(F), 'face flag words', sorted(set(F[:, 0].tolist()))[:8])
        print('   loaded bbox', P.min(0), P.max(0), 'radius', int(np.sqrt((P ** 2).sum(1)).max()))
        print('   sro u,v words (first 6 vertices)', V[:6, 3:].tolist())
    # views
    img = Image.new('RGB', (1200, 600), (0, 0, 0)); dr = ImageDraw.Draw(img)
    cols = {'mesta21': (255, 80, 80), 'mesta22': (80, 255, 80), 'mesta23': (80, 120, 255)}
    def proj_top(p): return (300 + p[0] * 0.3, 300 - p[2] * 0.3)
    def proj_side(p): return (900 + p[0] * 0.3, 300 - p[1] * 0.3)
    for n, c in cols.items():
        P, Fi, _, _ = loaded('temple/%s.sro' % n)
        for f in Fi:
            for pr in (proj_top, proj_side):
                pts = [pr(P[k]) for k in f] + [pr(P[f[0]])]
                dr.line(pts, fill=c)
    # putki 3, 4 (pivot moved, then R(...) and position (0,-150,0)); putki 5 at (0,100,0) with R(pi/2,0,0)
    for (sx, sy, sz, piv, pos) in ((1.5, 1.5, 5.3, True, (0, -150, 0)), (3, 3, 4.6, False, (0, 100, 0))):
        P, Fi = putki(sx, sy, sz, piv)
        a = math.pi / 2
        Rx = np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]])
        Q = (Rx @ P.T).T + np.array(pos)
        for f in Fi:
            for pr in (proj_top, proj_side):
                dr.line([pr(Q[k]) for k in f] + [pr(Q[f[0]])], fill=(255, 120, 255))
    # camera path radius range (k 0.3 .. 0.9): circle radius 850k at height 320k
    for k in (0.3, 0.9):
        r = 850 * k
        dr.ellipse([300 - r * 0.3, 300 - r * 0.3, 300 + r * 0.3, 300 + r * 0.3], outline=(255, 255, 0))
        dr.line([(900 - r * 0.3, 300 - 320 * k * 0.3), (900 + r * 0.3, 300 - 320 * k * 0.3)], fill=(255, 255, 0))
    dr.text((10, 10), 'top view (x right, z up): mesta21 red, 22 green, 23 blue, pipes magenta, camera yellow', fill=(255, 255, 255))
    dr.text((610, 10), 'side view (x right, y up)', fill=(255, 255, 255))
    img.save('out/meshes.png')
