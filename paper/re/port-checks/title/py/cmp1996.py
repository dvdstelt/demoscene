import pickle
from tmodel import *
from PIL import Image
states = pickle.load(open('b1996_states.pkl', 'rb'))
buf, base = model.b1996(G)
fb = buf.astype(np.float64)
def f90f(a, start, count, pitch):
    idx = np.arange(start, start + count)
    return (a[idx - pitch] + a[idx + pitch] + a[idx - 1] + a[idx + 1]) / 4
def f47f(flat, start, rows, pitch):
    n = rows * pitch
    p1 = f90f(flat, start, n, pitch)
    tmp = np.concatenate([np.zeros(640), p1, np.zeros(640)])
    flat[start:start+n] = f90f(tmp, 640, n, pitch)
fl = {}
for k in range(1, 258):
    f47f(fb, base, 200, 320)
    if k in (150, 200, 257): fl[k] = fb[base:base+64000].copy()
for k in (150, 200, 257):
    e = states[k-150].astype(float)
    print(k, 'exact max %.1f mean %.2f' % (e.max(), e.mean()), 'float max %.1f mean %.2f' % (fl[k].max(), fl[k].mean()))
rows = []
for k in (150, 200, 257):
    e = states[k-150].astype(float).reshape(200, 320); f = fl[k].reshape(200, 320)
    rows.append(np.concatenate([e, f], axis=1))
img = np.concatenate(rows, axis=0)
Image.fromarray(np.clip(img * 2, 0, 255).astype(np.uint8)).save('/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/part-title/b1996_cmp.png')
