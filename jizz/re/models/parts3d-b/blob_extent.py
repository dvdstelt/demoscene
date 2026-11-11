"""Sample the blob model over the whole part (T = 0 .. 30.4e6 PIT clocks, every 0.05 s) and report the mesh size
and whether the surface reaches the edge of the polygonisation box [-1024, 960]^3 (open holes there).
usage: python3 blob_extent.py"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, blobs, snap
M = snap.Mem(7506); sin = [M.i(0xfb9e + 4 * i) for i in range(1280)]
mx = np.zeros(3); mn = np.zeros(3); nv = []; nf = []; hits = []
for T in range(0, 30_400_000, 59659):
    P = blobs.balls(T, sin)
    V, N, F = blobs.build(P)
    nv.append(len(V)); nf.append(len(F))
    mx = np.maximum(mx, V.max(0)); mn = np.minimum(mn, V.min(0))
    if (V.max(0) >= 960).any() or (V.min(0) <= -1024).any(): hits.append(T)
print('verts min/max', min(nv), max(nv), 'faces min/max', min(nf), max(nf))
print('extent min', mn, 'max', mx)
print('frames touching the box edge:', len(hits), 'of', len(nv), [round(t / 1193182, 2) for t in hits[:20]])
