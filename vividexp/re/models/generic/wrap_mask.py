"""Mark the terrain faces whose mapping coordinates span more than half the texture (drawn "the long way") on a
model frame: runs/wrap_<t>.png (left the frame, right with those faces tinted)."""
import sys
import numpy as np
from PIL import Image
import render
from gen import s16

ini, t = sys.argv[1], int(sys.argv[2])
sc = render.Scene(ini)
o = sc.objs[0]
uv = o.m3d.uv >> 2
faces = o.m3d.groups[0][1]
span = np.maximum(np.ptp(uv[faces[:, :3], 0], 1), np.ptp(uv[faces[:, :3], 1], 1))
wrap = set(np.nonzero(span > 8192)[0].tolist())
page = render.render_frame(sc, [t])
ids = np.full((200, 320), -1, np.int64)


class Recorder:
    """a page that records which face wrote each pixel"""
    def __init__(self):
        self.cur = 0

    def __setitem__(self, yx, v):
        ids[yx] = self.cur


rec = Recorder()
# re-run the frame recording which face index lands on each pixel
pos, vals = sc.state(o, t)
v, nrm = render.transform(o, vals)
sx, sy = render.project(v, vals[0], vals[1], vals[2])
keys = []
for fi, f in enumerate(faces):
    a, b, c = f[0], f[1], f[2]
    p1 = s16(sy[a] - sy[b]) * s16(sx[c] - sx[b]); p2 = s16(sx[a] - sx[b]) * s16(sy[c] - sy[b])
    if p2 < p1 or not all(int(v[2][f[k]]) + vals[2] > -6720 for k in range(3)):
        continue
    zs = sorted([int(v[2][f[k]]) for k in range(3)])
    keys.append((fi, s16(zs[0] + (s16(zs[2] - zs[0]) >> 1))))
keys = render.hpsort_desc(keys, key=lambda k: k[1])
for fi, _ in keys:
    f = faces[fi]
    rec.cur = fi
    render.raster(rec, [(int(sx[f[k]]), int(sy[f[k]]), 0, 0, 0, 0) for k in range(3)], lambda a, b, c, d: 0)
pal = np.frombuffer(open(render.gen.path(sc.cfg['Palette']), 'rb').read(), np.uint8).reshape(256, 3).astype(np.uint32)
lut = ((pal * 255 + 31) // 63).astype(np.uint8)
img = lut[page.astype(np.uint8)]
m = np.isin(ids, list(wrap))
tint = img.copy()
tint[m] = (tint[m] * 0.4 + np.array([0, 160, 255]) * 0.6).astype(np.uint8)
print('t', t, 'pixels of wrapping faces', int(m.sum()))
Image.fromarray(np.concatenate([img, tint], 1)).resize((1280, 400), Image.NEAREST).save('runs/wrap_%d.png' % t)
