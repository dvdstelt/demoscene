"""How visible is the 15-bit wrap of the bump coordinates (p3 1bb0: shl/sar of the object-space normal) and the
p3 bump table's row-8 quirk? Renders a frame as the engine does and with each quirk removed (out/p3_quirks_<frame>.png:
engine | no 15-bit wrap | difference)."""
import sys, os
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import engine_model as em
import p3model
d = np.load(sys.argv[1])
pal = np.load(os.path.join(HERE, 'out', 'p3_pal.npy'))
rgb = ((pal.astype(np.uint16) * 255 + 31) // 63).astype(np.uint8)
orig = em.transform
def nowrap(mesh, M, pos, pivot, zoom, kind):
    out = orig(mesh, M, pos, pivot, zoom, kind)
    res = []
    for i, (s, dep, A, B) in enumerate(out):
        nx, ny = int(mesh.N[i][0]), int(mesh.N[i][1])
        res.append((s, dep, ((ny & 0xffff) << 16) | (nx & 0xffff), B))   # full 16-bit normal: no 15-bit wrap
    return res
cnt = 0
F = p3model.MESH.F; N = p3model.MESH.N
for f in F:
    v = N[f[1:] >> 2]
    for c in (0, 1):
        s = np.sign(np.abs(v[:, c]) - 16384)
        if s.min() < 0 < s.max(): cnt += 1; break
print('faces straddling |nx| or |ny| = 16384:', cnt, 'of', len(F))
for k in sorted(int(n[3:]) for n in d.files if n.startswith('fb_'))[2:4]:
    lab = int(d['label'][k]); lab = lab - 0x10000 if lab & 0x8000 else lab
    a = p3model.render_frame(d['o0'][k], lab)
    em.transform = nowrap; p3model.em.transform = nowrap
    b = p3model.render_frame(d['o0'][k], lab)
    em.transform = orig; p3model.em.transform = orig
    print('frame', d['frame'][k], 'pixels changed without the wrap', (a != b).sum())
    im = np.concatenate([rgb[a.reshape(200, 320)], rgb[b.reshape(200, 320)], np.where((a != b).reshape(200, 320)[..., None], 255, 0).astype(np.uint8).repeat(3, 2)], 1)
    Image.fromarray(im).resize((im.shape[1] * 2, 400), Image.NEAREST).save(os.path.join(HERE, 'out', 'p3_quirks_%d.png' % d['frame'][k]))
