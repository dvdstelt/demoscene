"""Mark on the model frame where the faces the near test drops would be (clipped at depth 1): runs/near_<t>.png."""
import sys
import numpy as np
from PIL import Image, ImageDraw
import render
from near_check4 import clip, area  # noqa  (re-uses the clipper; near_check4 runs nothing on import with no args)

ini, t = sys.argv[1], int(sys.argv[2])
sc = render.Scene(ini)
mask = Image.new('L', (320, 200), 0)
dr = ImageDraw.Draw(mask)
for o in sc.objs:
    pos, vals = sc.state(o, t)
    v, nrm = render.transform(o, vals)
    X, Y, Z = vals[:3]
    P = np.stack([v[0] + X, v[1] + Y, v[2] + Z + 7000], 1).astype(float)
    for fc in o.m3d.groups[0][1]:
        tri = [P[fc[k]] for k in range(3)]
        if min(p[2] for p in tri) > 280:
            continue
        poly = clip(tri, lambda p: p[2] >= 1.0, lambda p, q: p + (q - p) * ((1.0 - p[2]) / (q[2] - p[2])))
        if len(poly) < 3:
            continue
        scr = [(160 + 371 * p[0] / p[2], 100 - 309 * p[1] / p[2]) for p in poly]
        if area(scr) >= 0:
            continue
        scr = [(max(-1e5, min(1e5, x)), max(-1e5, min(1e5, y))) for x, y in scr]
        dr.polygon(scr, fill=255)
page = render.render_frame(sc, [t] * len(sc.objs))
pal = np.frombuffer(open(render.gen.path(sc.cfg['Palette']), 'rb').read(), np.uint8).reshape(256, 3).astype(np.uint32)
lut = ((pal * 255 + 31) // 63).astype(np.uint8)
img = lut[page.astype(np.uint8)].copy()
m = np.asarray(mask) > 0
img[m] = (img[m] * 0.5 + np.array([0, 120, 255]) * 0.5).astype(np.uint8)
print('t', t, 'pixels under dropped near faces', int(m.sum()))
Image.fromarray(np.concatenate([lut[page.astype(np.uint8)], img], 1)).resize((1280, 400), Image.NEAREST).save('runs/near_%d.png' % t)
