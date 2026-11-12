"""Nearest vertex depth per drawn frame (does anything reach the camera plane? the engine has no near clipping)."""
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import engine_model as em
for part, npz in (('p3', sys.argv[1]), ('p5', sys.argv[2])):
    mod = __import__(part + 'model')
    d = np.load(npz)
    V = mod.MESH.V.astype(np.int64)
    worst = (None, 1e18)
    for k in range(0, len(d['frame'])):
        cam = mod.obj_fields(bytes(d['o0'][k]))
        if part == 'p3':
            obj = mod.obj_fields(mod.IMG[mod.OBJ4:mod.OBJ4 + 0x3a])
        else:
            obj = mod.obj_fields(bytes(d['o1'][k]))
        M = np.array(em.matrix(mod.SIN, *(((obj['ang'][i] + cam['ang'][i]) & 0xffff) for i in range(3))), np.int64)
        pz = em.s32(obj['pos'][2] + cam['pos'][2])
        Z = V @ M[2]
        zd = Z - (pz << 8)
        m = -zd.max() / 32768.0          # distance of the nearest vertex in vertex units (zd < 0 = in front)
        if m < worst[1]:
            worst = (int(d['frame'][k]), m)
    print(part, 'nearest vertex over the part: frame %d, distance %.0f vertex units' % worst)
