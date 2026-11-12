"""Orthographic point/edge views of p2's meshes (out/mesh_<seg>.png): front (x,y), top (x,z), side (z,y)."""
import os, sys
import numpy as np
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mesh import mesh

for seg in (0x1e71, 0x2121):
    m = mesh(seg)
    V = m['V']; F = m['F'][:, 1:] // 4
    im = Image.new('RGB', (900, 300), 'black'); dr = ImageDraw.Draw(im)
    for k, (i, j) in enumerate(((0, 1), (0, 2), (2, 1))):
        def p(v): return (k * 300 + 150 + v[i] * 140 / 8192, 150 - v[j] * 140 / 8192)
        for f in F:
            a, b, c = (p(V[t]) for t in f)
            dr.line([a, b, c, a], fill=(90, 90, 200))
    im.save(os.path.join(HERE, 'out', 'mesh_%04x.png' % seg))
    # face winding / sizes
    print(hex(seg), 'faces use vertices', F.min(), F.max())
