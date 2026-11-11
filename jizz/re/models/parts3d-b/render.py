"""Approximate re-render of a 3D frame of the parts3d-b parts, to compare with the page the engine drew.

usage: python3 render.py DUMP.pkl OUT.png [--model]
  default: per-vertex screen positions / UVs / env UVs / faces as the engine left them in the dump
  --model: geometry from objects.py / blobs.py and camera from camera.py (the notes' formulas) instead
Shading as read from the span filler 0x1e07c (inner loop 0x1e7d5): texel (u, v) of the face's texture (slot face+0xe,
interleaved RGB), light L = slot 0 (0x220000, 256x256 bytes built by 0x6592) at the env coordinates,
pixel = TR[R + L] + TG[G + L] + TB[B + L] (tables 0x1a479 / 0x1b079 / 0x1bc79). Approximations: z-buffer instead of
the radix sort, screen-space affine UVs, nearest texel, no sub-pixel rules, near clip by dropping faces.
"""
import sys, os, pickle, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np

d = pickle.load(open(sys.argv[1], 'rb')); m = d['mem']
rd = lambda a: struct.unpack_from('<I', m, a)[0]
rf = lambda a: struct.unpack_from('<f', m, a)[0]
use_model = '--model' in sys.argv
TR = np.frombuffer(m[0x1a479:0x1a479 + 3072], '<u4'); TG = np.frombuffer(m[0x1b079:0x1b079 + 3072], '<u4')
TB = np.frombuffer(m[0x1bc79:0x1bc79 + 3072], '<u4')
slot = lambda k: rd(0x654a + 4 * k)
L = np.frombuffer(m[slot(0):slot(0) + 65536], np.uint8)
VB, FB, OB = rd(0x18d4c), rd(0x18d48), rd(0x18d50)

tris = []   # (screen xy[3], z[3], uv[3], env[3], tex)
if not use_model:
    for oi in range(rd(0x18d58)):
        o = OB + 0x50 * oi
        f0, nf = rd(o), rd(o + 4)
        for fi in range(f0, f0 + nf):
            fa = FB + 0x1c * fi
            vp = [rd(fa), rd(fa + 4), rd(fa + 8)]
            R = [np.frombuffer(m[p:p + 0x40], np.float32) for p in vp]
            flags = [m[p + 0x3d] for p in vp]
            if any(f == 4 for f in flags): continue        # +0x3d = 4: z <= 16, the engine drops the face
            uv = [(r[11], r[12]) for r in R]
            if m[fa + 0xc] & 0x80:                         # wrapped UVs from the per-face buffer
                w = np.frombuffer(m[rd(fa + 0x10):rd(fa + 0x10) + 24], np.float32); uv = [(w[0], w[1]), (w[2], w[3]), (w[4], w[5])]
            tris.append(([(r[6] / 65536, r[7]) for r in R], [r[5] for r in R], uv, [(r[13], r[14]) for r in R], m[fa + 0xe]))
else:
    import camera, objects, blobs
    T, k, dist = rf(0x159f7), rd(0x157a0), rf(0x154b4)
    script = {1: 0x157fa, 3: 0x15973, 4: 0x159cb}[k]
    eye, tgt, M, trans, roll = camera.camera(T, k, dist)
    ts, dist2, objs = objects.run(script)
    if k == 4:   # blobs: ball positions from the ball time (the camera T is read ~12 ms later; use memory's balls)
        P = [(struct.unpack_from('<i', m, 0x7918 + 4 * i)[0], struct.unpack_from('<i', m, 0x7930 + 4 * i)[0],
              struct.unpack_from('<i', m, 0x7948 + 4 * i)[0]) for i in range(6)]
        V, N, F = blobs.build(P)
        o = objects.Obj(V, N[:, 0:2].copy(), F, m[0x6b6c], 3); o.N = N; objs.append(o)
    for o in objs:
        c = (M.astype(np.float64) @ (o.V.astype(np.float64) - tgt).T).T
        z = c[:, 2] + dist
        sx = 160 + c[:, 0] * dist / z; sy = 100 - c[:, 1] * dist / z
        n = o.N.astype(np.float64); n /= np.linalg.norm(n, axis=1, keepdims=True)
        eu = (128 + 128 * (n @ M[0])) * 65536; ev = (128 + 128 * (n @ M[1])) * 65536
        for (i0, i1, i2) in o.F:
            ii = (i0, i1, i2)
            if any(z[i] <= 16 for i in ii): continue
            uv = [tuple(o.UV[i]) for i in ii]
            us = [u for u, _ in uv]; vs = [v for _, v in uv]
            # 0x14651: fold UVs into [0, 2^24) and undo jumps of >= 128 texels between vertices
            uv = [(np.fmod(u, 16777216.0), np.fmod(v, 16777216.0)) for u, v in uv]
            uu = [a for a, _ in uv]; vv = [b for _, b in uv]
            for arr in (uu, vv):
                for a, b in ((0, 1), (1, 2), (2, 0)):
                    if abs(arr[a] - arr[b]) >= 8388608: arr[b] += 16777216.0 if arr[a] > arr[b] else -16777216.0
            uv = list(zip(uu, vv))
            tris.append(([(sx[i], sy[i]) for i in ii], [z[i] for i in ii], uv, [(eu[i], ev[i]) for i in ii], o.tex))

img = np.zeros((200, 320), np.uint32); zb = np.full((200, 320), np.inf)
for xy, zz, uv, env, tex in tris:
    (x0, y0), (x1, y1), (x2, y2) = xy
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if area <= 0: continue                                  # back face (engine: sign bit of the cross product)
    xa, xb = int(max(0, np.floor(min(x0, x1, x2)))), int(min(319, np.ceil(max(x0, x1, x2))))
    ya, yb = int(max(0, np.floor(min(y0, y1, y2)))), int(min(199, np.ceil(max(y0, y1, y2))))
    if xa > xb or ya > yb: continue
    X, Y = np.meshgrid(np.arange(xa, xb + 1) + 0.5, np.arange(ya, yb + 1) + 0.5)
    w0 = ((x1 - X) * (y2 - Y) - (x2 - X) * (y1 - Y)) / area
    w1 = ((x2 - X) * (y0 - Y) - (x0 - X) * (y2 - Y)) / area
    w2 = 1 - w0 - w1
    ins = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
    if not ins.any(): continue
    z = w0 * zz[0] + w1 * zz[1] + w2 * zz[2]
    zsub = zb[ya:yb + 1, xa:xb + 1]
    ins &= z < zsub
    if not ins.any(): continue
    u = (w0 * uv[0][0] + w1 * uv[1][0] + w2 * uv[2][0])[ins]; v = (w0 * uv[0][1] + w1 * uv[1][1] + w2 * uv[2][1])[ins]
    eu = (w0 * env[0][0] + w1 * env[1][0] + w2 * env[2][0])[ins]; ev = (w0 * env[0][1] + w1 * env[1][1] + w2 * env[2][1])[ins]
    ui = (np.floor(u / 65536).astype(np.int64)) & 255; vi = (np.floor(v / 65536).astype(np.int64)) & 255
    li = L[((np.floor(ev / 65536).astype(np.int64) & 255) << 8) | (np.floor(eu / 65536).astype(np.int64) & 255)].astype(np.int64)
    base = slot(tex) + (vi * 256 + ui) * 3
    mm = np.frombuffer(m, np.uint8)
    r, g, b = mm[base].astype(np.int64), mm[base + 1].astype(np.int64), mm[base + 2].astype(np.int64)
    pix = TR[r + li] + TG[g + li] + TB[b + li]
    sub = img[ya:yb + 1, xa:xb + 1]; sub[ins] = pix; zsub[ins] = z[ins]

page = np.frombuffer(d['page'], '<u2').reshape(200, 320).astype(np.uint32)
def rgb(a): return np.stack([((a >> 11) & 31) * 255 // 31, ((a >> 5) & 63) * 255 // 63, (a & 31) * 255 // 31], -1).astype(np.uint8)
A, B = rgb(img), rgb(page)
diff = np.abs(A.astype(int) - B.astype(int)).max(-1)
print(f'{len(tris)} triangles; mean |diff| {np.abs(A.astype(int) - B.astype(int)).mean():.2f}/255, '
      f'pixels within 8/255: {(diff <= 8).mean() * 100:.1f}%, within 24/255: {(diff <= 24).mean() * 100:.1f}%, exact: {(img == page).mean() * 100:.1f}%')
from PIL import Image
Image.fromarray(np.concatenate([B, A, np.repeat(np.minimum(diff * 4, 255).astype(np.uint8)[:, :, None], 3, -1)], 1)).resize((1920, 400), Image.NEAREST).save(sys.argv[2])
