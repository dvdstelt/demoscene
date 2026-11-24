"""Vertices close to or behind the camera: how often the z <= 0 branch runs, and how often an edge's stamp count
(n mod 256, the loop counts down cl only) cuts a line short while it is still on the screen."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sheet_model as sm
from sheet_model import Tables, State, heights
T = Tables()
S = T.sin


def depth(st, z):
    a40, a44, a68 = sm.angles(T, st)
    out = []
    for k in range(256):
        x, y, zz = T.gx[k], T.gy[k], z[k]
        s1, c1 = S[a68], S[a68 + 1025]; s2, c2 = S[a44], S[a44 + 1025]; s3, c3 = S[a40], S[a40 + 1025]
        x1 = (x * c1 - y * s1) >> 10; y1 = (y * c1 + x * s1) >> 10
        x2 = (x1 * c2 - zz * s2) >> 10; z1 = (zz * c2 + x1 * s2) >> 10
        z2 = (z1 * c3 + y1 * s3) >> 10
        out.append(z2 + T.camz)
    return out


behind_frames = behind_pts = 0
cut = cut_frames = 0
zmin = 1 << 30
for c in range(0, 1138):
    st = State(T, c)
    z = heights(T, st)
    d = depth(st, z)
    nb = sum(1 for v in d if v <= 0)
    zmin = min(zmin, min(d))
    behind_pts += nb
    behind_frames += nb > 0
    pts = sm.project(T, st, z)
    fc = 0
    for e0, e1 in T.edges:
        (x0, y0), (x1, y1) = pts[e0], pts[e1]
        if not (y0 & 0xffffffff) < (y1 & 0xffffffff):
            x0, y0, x1, y1 = x1, y1, x0, y0
        dx, dy = x1 - x0, y1 - y0
        n = max(dx + 1 if dx >= 0 else -dx, dy + 1 if dy >= 0 else -dy) >> 1
        if n > 255 and 0 <= x0 < 320 and 0 <= y0 < 200:
            cnt = ((n - 1) & 0xff) + 1
            ex, ey = x0 + dx * cnt / n, y0 + dy * cnt / n       # where the stamps stop
            if 0 <= ex < 312 and 0 <= ey < 192:
                fc += 1
    cut += fc
    cut_frames += fc > 0
print('depth min over the part', zmin)
print('frames with vertices behind the camera (z <= 0): %d of 1138, %.2f vertices per frame' % (behind_frames, behind_pts / 1138))
print('edges cut short on screen by the 8-bit stamp count: %d frames, %d edges in all' % (cut_frames, cut))

# edges with an end behind the camera that leave stamps on the screen
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stats import stamp_positions  # noqa: E402  (runs stats.py's statistics too)
vis_frames = vis_edges = 0
for c in range(0, 1138):
    st = State(T, c)
    z = heights(T, st)
    d = depth(st, z)
    if min(d) > 0:
        continue
    pts = sm.project(T, st, z)
    ve = 0
    for e0, e1 in T.edges:
        if d[e0] <= 0 or d[e1] <= 0:
            if stamp_positions(*pts[e0], *pts[e1]):
                ve += 1
    vis_edges += ve
    vis_frames += ve > 0
print('edges with an end behind the camera that put stamps on the screen: %d in %d frames' % (vis_edges, vis_frames))
