"""Screen pixels the warp grid does not cover (pictures 1 and 2), per shown refresh N.

The grid (x, z in -1400 .. 1050, camera at z = -1300, orientation Rx(1.5) Ry(3.0 + 0.01 m)) is tilted 4 degrees
from facing the camera, so at some rotations a screen corner lies outside it; those pixels keep what the draw
buffer held (the frame drawn two frames earlier), unless the sprite covers them.
m(N) = N - 4255 for N <= 4612 (picture 2), m(N) = N - 9784 for 10361 <= N <= 10573 (picture 1) (models/sprites/
angles.py). Prints the refreshes with uncovered pixels and checks them against full.frames (an uncovered pixel
keeps the value of refresh N - 2).
usage: python3 models/sprites/uncovered.py
"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
from warp_model import Rx, Ry
from sheet import open_frames, indexed

d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
SPR = {1: (np.frombuffer(d.get('nigga/sprite1.raw'), np.uint8).reshape(156, 164), 0, 44),
       2: (np.frombuffer(d.get('nigga/sprite2.raw'), np.uint8).reshape(200, 172), 0, 0)}
edge = [(-1400 + 350 * i, -1400) for i in range(7)] + [(1050, -1400 + 350 * j) for j in range(7)] + \
       [(1050 - 350 * i, 1050) for i in range(7)] + [(-1400, 1050 - 350 * j) for j in range(7)]
ys, xs = np.mgrid[0:200, 0:320] + 0.5


def outside_mask(m):
    M = Rx(1.5) @ Ry(3.0 + 0.01 * m)
    pts = []
    for x, z in edge:
        p = M @ np.array([x, 0, z], float) + np.array([0, 0, 1300])
        pts.append((160 + 256 * p[0] / p[2], 100 + 256 * 5 / 6 * p[1] / p[2]))
    inside = np.zeros((200, 320), bool)
    n = len(pts)
    for i in range(n):
        (x1, y1), (x2, y2) = pts[i], pts[(i + 1) % n]
        cross = ((y1 > ys) != (y2 > ys)) & (xs < x1 + (ys - y1) * (x2 - x1) / (y2 - y1 + 1e-12))
        inside ^= cross
    return ~inside


raw, _ = open_frames(os.path.join(HERE, '..', '..', 'full.frames'))
for pic, N0, N1, off in ((2, 4257, 4611, 4255), (1, 10361, 10573, 9784)):
    spr, sx, sy = SPR[pic]
    sm = np.zeros((200, 320), bool)
    sm[sy:sy + spr.shape[0], sx:sx + spr.shape[1]] = spr != 0
    runs = []
    for N in range(N0, N1 + 1):
        out = outside_mask(N - off) & ~sm
        if out.any():
            px, _ = indexed(raw, N)
            old, _ = indexed(raw, N - 2)
            runs.append((N, int(out.sum()), float((px[out] == old[out]).mean())))
    if runs:
        print(f'picture {pic}: {len(runs)} refreshes with uncovered pixels, {runs[0][0]}..{runs[-1][0]}, '
              f'max {max(r[1] for r in runs)} px; share of those pixels equal to refresh N-2: '
              f'{np.mean([r[2] for r in runs]):.3f}')
        for r in runs[::max(1, len(runs) // 8)]:
            print('   N %d: %d px, equal to N-2: %.3f' % r)
    else:
        print(f'picture {pic}: every pixel the sprite leaves visible is covered')
    allout = [(N, int(outside_mask(N - off).sum())) for N in range(N0, N1 + 1)]
    allout = [t for t in allout if t[1]]
    print(f'   refreshes with screen pixels outside the grid (all hidden by the sprite): {len(allout)}',
          (allout[0], allout[-1], max(t[1] for t in allout)) if allout else '')

# the corner margin in pixels (negative = the screen corner lies outside the grid) over the visible range of m
def corner_margin(m):
    M = Rx(1.5) @ Ry(3.0 + 0.01 * m)
    pts = []
    for x, z in edge:
        p = M @ np.array([x, 0, z], float) + np.array([0, 0, 1300])
        pts.append((160 + 256 * p[0] / p[2], 100 + 256 * 5 / 6 * p[1] / p[2]))
    P = np.array(pts + pts[:1])
    a, b = P[:-1], P[1:]
    worst = 1e9
    for c in np.array([[0, 0], [320, 0], [320, 200], [0, 200]], float):
        t = np.clip(((c - a) * (b - a)).sum(1) / ((b - a) ** 2).sum(1), 0, 1)
        dist = np.sqrt((((a + (b - a) * t[:, None]) - c) ** 2).sum(1)).min()
        crossings = sum(1 for (x1, y1), (x2, y2) in zip(a, b)
                        if (y1 > c[1]) != (y2 > c[1]) and c[0] < x1 + (c[1] - y1) * (x2 - x1) / (y2 - y1))
        worst = min(worst, dist if crossings % 2 else -dist)
    return worst


for pic, ms in ((2, np.arange(0, 358, 0.25)), (1, np.arange(577, 790, 0.25))):
    mm = [(corner_margin(m), m) for m in ms]
    w = min(mm)
    print(f'picture {pic}: smallest corner margin {w[0]:.3f} px at m = {w[1]}')
