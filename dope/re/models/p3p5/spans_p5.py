"""p5 ENV frames: does the env filler's 8-bit span counter (2370/2420: `dec al`) or its right-edge clip at x = 318
ever matter? Projects the torus for every drawn ENV frame (hookrun npz) and reports the object's screen extent;
for frames wider than 255 px renders the frame with and without the 8-bit counter."""
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import engine_model as em
import p5model
d = np.load(sys.argv[1])
wide = []; clip = []
for k in range(len(d['frame'])):
    o1 = d['o1'][k]
    if int(o1[0x2a]) != 0:
        continue
    cam = p5model.obj_fields(bytes(d['o0'][k])); obj = p5model.obj_fields(bytes(o1))
    M = em.matrix(p5model.SIN, *(((obj['ang'][i] + cam['ang'][i]) & 0xffff) for i in range(3)))
    pos = tuple(em.s32(obj['pos'][i] + cam['pos'][i]) for i in range(3))
    vs = em.transform(p5model.MESH, M, pos, obj['pivot'], obj['zoom'], 'uv')
    sx = np.array([em.s16(v[0] >> 16) for v in vs]); sy = np.array([em.s16(v[0]) for v in vs])
    vis = (sy >= 1) & (sy <= 200)
    if not vis.any():
        continue
    w = sx.max() - max(sx.min(), 0)
    if min(sx.max(), 319) - max(sx.min(), 0) > 255:
        wide.append((int(d['frame'][k]), int(sx.min()), int(sx.max())))
    if sx.max() >= 319 and sx.min() < 319:
        clip.append(int(d['frame'][k]))
print('ENV frames with the object wider than 255 px on screen:', len(wide), wide[:5], wide[-3:])
print('ENV frames where the object reaches x >= 319:', len(clip), clip[:3], clip[-3:])
