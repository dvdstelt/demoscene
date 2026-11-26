"""Ranges of the projection over the part: smallest depth, extent on screen, vertices off screen."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sheet_model as sm
from sheet_model import Tables, State, heights, project, angles
T = Tables()
zmin, zmax, xr, yr = 1 << 30, -(1 << 30), [1 << 30, -(1 << 30)], [1 << 30, -(1 << 30)]
orig = sm.div8
depths = []
def spy(v, d):
    depths.append(d)
    return orig(v, d)
sm.div8 = spy
offs = 0
for c in range(0, 1202):
    st = State(T, c, max(0, c - 1137))
    depths.clear()
    pts = project(T, st, heights(T, st))
    zmin = min(zmin, min(depths)); zmax = max(zmax, max(depths))
    if c < 1138:
        for x, y in pts:
            xr = [min(xr[0], x), max(xr[1], x)]; yr = [min(yr[0], y), max(yr[1], y)]
            offs += not (0 <= x < 320 and 0 <= y < 200)
print('depth (z2 + 2048, used twice per point) min %d max %d' % (zmin, zmax))
print('before order 27: screen x %s y %s; vertices off screen %.1f per frame' % (xr, yr, offs / 1138))
print('angles at c=0..', [angles(T, State(T, c)) for c in (0, 100, 200, 300)])
