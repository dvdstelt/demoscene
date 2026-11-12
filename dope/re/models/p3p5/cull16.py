"""How often does the engine's 16-bit back-face product (2887) give a different answer than exact arithmetic?"""
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import engine_model as em
def s16(a): return ((a + 0x8000) & 0xffff) - 0x8000
for part, npz in (('p3', sys.argv[1]), ('p5', sys.argv[2])):
    mod = __import__(part + 'model')
    d = np.load(npz)
    F = mod.MESH.F[:, 1:] // 4
    tot = 0; frames = []
    for k in range(0, len(d['frame']), 5):
        cam = mod.obj_fields(bytes(d['o0'][k]))
        obj = mod.obj_fields(mod.IMG[mod.OBJ4:mod.OBJ4 + 0x3a]) if part == 'p3' else mod.obj_fields(bytes(d['o1'][k]))
        M = em.matrix(mod.SIN, *(((obj['ang'][i] + cam['ang'][i]) & 0xffff) for i in range(3)))
        pos = tuple(em.s32(obj['pos'][i] + cam['pos'][i]) for i in range(3))
        vs = em.transform(mod.MESH, M, pos, obj['pivot'], obj['zoom'], 'normal')
        sx = np.array([em.s16(v[0] >> 16) for v in vs], np.int64); sy = np.array([em.s16(v[0]) for v in vs], np.int64)
        a, b, c = F[:, 0], F[:, 1], F[:, 2]
        cx = (sx[c] - sx[a]) * (sy[b] - sy[a]); ax = (sx[b] - sx[a]) * (sy[c] - sy[a])
        exact = (ax - cx) >= 0
        e16 = s16(s16(s16(sx[b] - sx[a]) * s16(sy[c] - sy[a])) - s16(s16(sx[c] - sx[a]) * s16(sy[b] - sy[a]))) >= 0
        n = int((exact != e16).sum())
        if n:
            tot += n; frames.append((int(d['frame'][k]), n))
    print(part, 'sampled frames with 16-bit culling errors:', len(frames), 'faces', tot, frames[:8])
