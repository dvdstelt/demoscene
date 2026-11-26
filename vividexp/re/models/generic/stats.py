"""Mesh statistics: mapping-coordinate spans per face (faces where the texture runs "the long way"), vertex / normal pairs."""
import sys
import numpy as np
import render
import gen

ini = sys.argv[1]
sc = render.Scene(ini)
for o in sc.objs[:1]:
    m = o.m3d
    f = m.groups[0][1]
    if m.uv is not None:
        uv = m.uv >> 2
        du = np.abs(uv[f[:, [0, 1, 2]], 0][:, :, None] - uv[f[:, [0, 1, 2]], 0][:, None, :]).max((1, 2))
        dv = np.abs(uv[f[:, [0, 1, 2]], 1][:, :, None] - uv[f[:, [0, 1, 2]], 1][:, None, :]).max((1, 2))
        print(o.name, 'faces', len(f), 'max uv span in a face (8.6):', du.max(), dv.max(), 'faces with span > 8192:',
              int(((du > 8192) | (dv > 8192)).sum()))
    vi = f[:, :3].ravel(); ni = f[:, 3:].ravel()
    print('  distinct (vertex, normal) pairs', len(set(zip(vi.tolist(), ni.tolist()))), 'vertices', len(m.xyz), 'normals', len(m.normals))
